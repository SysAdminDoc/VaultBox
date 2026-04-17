// VaultBox Desktop - HTTP Server (Bitwarden-compatible API + GUI API)
#pragma once
#include "vaultbox_server.h"
#include "vaultbox_db.h"
#include "vaultbox_ui.h"

inline void setup_routes(httplib::Server& svr) {

    // Cap incoming payloads to protect against DoS via unbounded bodies.
    // 64 MB is plenty for any legitimate import/export.
    svr.set_payload_max_length(64 * 1024 * 1024);

    // Shared predicate used by both pre-routing (for origin enforcement) and
    // post-routing (for CORS header emission) handlers.
    auto classify_origin = [](const httplib::Request& req) {
        struct Info { std::string origin; bool allowed; bool is_local; bool is_extension; bool is_vaultbox_api; };
        Info info;
        info.origin = req.get_header_value("Origin");
        const std::string& path = req.path;
        info.is_vaultbox_api = (path.rfind("/api/vaultbox/", 0) == 0 || path == "/api/vaultbox");
        info.is_local = (info.origin == "http://127.0.0.1:8787" || info.origin == "http://localhost:8787");
        info.is_extension = (info.origin.rfind("chrome-extension://", 0) == 0
                          || info.origin.rfind("moz-extension://", 0) == 0);
        if (info.is_vaultbox_api) {
            // GUI API: same-origin SPA only (plus same-origin fetches with no Origin header).
            info.allowed = info.origin.empty() || info.is_local;
        } else {
            // Bitwarden-compat API: allow the VaultBox browser extension too.
            info.allowed = info.origin.empty() || info.is_local || info.is_extension;
        }
        return info;
    };

    // Block disallowed cross-origin requests before the route runs. This prevents
    // a malicious browser extension from invoking privileged /api/vaultbox/* endpoints
    // (e.g. /api/vaultbox/vault or /api/vaultbox/export) while the vault is unlocked.
    svr.set_pre_routing_handler([classify_origin](const httplib::Request& req, httplib::Response& res) {
        if (req.method == "OPTIONS") return httplib::Server::HandlerResponse::Unhandled;
        auto info = classify_origin(req);
        if (!info.allowed) {
            res.status = 403;
            res.set_content(json({{"message","Origin not allowed"}}).dump(), "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }

        // Any non-trivial vault-touching hit resets the server auto-lock watchdog.
        // Skip the chatty polling endpoints so that a hidden SPA polling /logs
        // every 2 seconds doesn't defeat auto-lock.
        const std::string& p = req.path;
        bool is_poll = (p == "/alive" || p == "/api/alive"
                     || p == "/api/vaultbox/logs"
                     || p == "/api/vaultbox/status");
        if (!is_poll) touch_activity();

        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Apply CORS + security headers on the way out.
    svr.set_post_routing_handler([classify_origin](const httplib::Request& req, httplib::Response& res) {
        auto info = classify_origin(req);
        std::string cors_origin = (info.allowed && !info.origin.empty()) ? info.origin : "http://127.0.0.1:8787";
        res.set_header("Access-Control-Allow-Origin", cors_origin);
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
            "Authorization, Content-Type, X-Requested-With, Device-Type, Bitwarden-Client-Name, Bitwarden-Client-Version");
        res.set_header("Access-Control-Allow-Credentials", "true");
        res.set_header("Access-Control-Max-Age", "600");
        res.set_header("Vary", "Origin");
        // API responses must not be cached anywhere - they can contain decrypted vault material.
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, private");
        res.set_header("Pragma", "no-cache");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Referrer-Policy", "no-referrer");
    });

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // --- Serve SPA at root ---
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        // Restrictive CSP: inline scripts/styles are required because the SPA is a
        // single-file HTML blob. Network fetches are restricted to localhost (own
        // origin) plus api.github.com for the opt-in update check. Images are
        // limited to data: URIs (no remote trackers, no DuckDuckGo favicons here
        // since this is the standalone SPA, not the Bitwarden extension).
        res.set_header("Content-Security-Policy",
            "default-src 'self'; "
            "script-src 'self' 'unsafe-inline'; "
            "style-src 'self' 'unsafe-inline'; "
            "img-src 'self' data:; "
            "connect-src 'self' https://api.github.com; "
            "frame-ancestors 'none'; "
            "base-uri 'self'; "
            "form-action 'self'");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Referrer-Policy", "no-referrer");
        res.set_header("Cache-Control", "no-store");
        res.set_content(VAULTBOX_SPA_HTML, "text/html; charset=utf-8");
    });
    svr.Get("/alive", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    svr.Get("/api/alive", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"server","VaultBox"}, {"version",APP_VERSION}, {"status","running"}});
    });

    // =================================================================
    // VaultBox GUI API (used by the embedded SPA)
    // =================================================================

    // --- Status ---
    svr.Get("/api/vaultbox/status", [](const httplib::Request&, httplib::Response& res) {
        std::string email;
        DB db;
        auto rows = db.query("SELECT email FROM accounts LIMIT 1", {});
        if (!rows.empty()) email = rows[0]["email"].get<std::string>();
        send_json(res, {{"unlocked", g_vault.unlocked.load()}, {"email", email}, {"version", APP_VERSION}});
    });

    // --- Unlock ---
    // Unlock is the only place an attacker (e.g. a malicious Chrome extension that
    // somehow got past the CORS filter, or another local process) can guess the
    // master password. PBKDF2 with 600K iterations already provides ~150ms of
    // work per attempt, but adding a short lockout after repeated failures
    // degrades an online brute force to near-useless without hurting a legitimate
    // user who typos once or twice.
    svr.Post("/api/vaultbox/unlock", [](const httplib::Request& req, httplib::Response& res) {
        static std::mutex lk_mtx;
        static int consecutive_failures = 0;
        static std::chrono::steady_clock::time_point lockout_until{};

        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        std::string password = body.value("password", "");
        if (email.empty() || password.empty()) {
            send_error(res, 400, "Email and password required"); return;
        }

        {
            std::lock_guard<std::mutex> g(lk_mtx);
            auto now = std::chrono::steady_clock::now();
            if (now < lockout_until) {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(lockout_until - now).count();
                send_error(res, 429, "Too many failed attempts. Try again in " + std::to_string(secs + 1) + "s.");
                return;
            }
        }

        bool ok = VBCrypto::unlock_vault(password, email);

        std::lock_guard<std::mutex> g(lk_mtx);
        if (ok) {
            consecutive_failures = 0;
            lockout_until = {};
            std::lock_guard<std::mutex> vl(g_vault.mtx);
            send_json(res, {{"success", true},
                            {"entries", (int)g_vault.entries.size()},
                            {"folders", (int)g_vault.folders.size()}});
        } else {
            consecutive_failures++;
            // Exponential-ish backoff once we've had more than 3 failures in a row.
            if (consecutive_failures >= 4) {
                int secs = std::min(60, 2 << std::min(consecutive_failures - 4, 5));
                lockout_until = std::chrono::steady_clock::now() + std::chrono::seconds(secs);
                vb_log("Unlock lockout engaged: " + std::to_string(secs) + "s after "
                       + std::to_string(consecutive_failures) + " failed attempts");
            }
            send_error(res, 401, "Invalid master password or account not found");
        }
    });

    // --- Lock ---
    svr.Post("/api/vaultbox/lock", [](const httplib::Request&, httplib::Response& res) {
        g_vault.clear();
        vb_log("Vault locked");
        send_json(res, {{"success", true}});
    });

    // --- Get vault data (decrypted) ---
    svr.Get("/api/vaultbox/vault", [](const httplib::Request&, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::lock_guard<std::mutex> lk(g_vault.mtx);
        json entries = json::array();
        for (auto& e : g_vault.entries) {
            entries.push_back({
                {"id", e.id}, {"name", e.name}, {"username", e.username},
                {"password", e.password}, {"uri", e.uri}, {"notes", e.notes},
                {"totp", e.totp}, {"type", e.type}, {"favorite", e.favorite},
                {"folderId", e.folderId}, {"folderName", e.folderName},
                {"updatedAt", e.updatedAt}
            });
        }
        json folders = json::array();
        for (auto& f : g_vault.folders) {
            folders.push_back({{"id", f.id}, {"name", f.name}});
        }
        send_json(res, {{"entries", entries}, {"folders", folders}});
    });

    // --- Create entry ---
    svr.Post("/api/vaultbox/entry", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        auto body = parse_body(req);
        DecryptedEntry de;
        de.type = body.value("type", 1);
        de.name = body.value("name", "");
        de.username = body.value("username", "");
        de.password = body.value("password", "");
        de.uri = body.value("uri", "");
        de.notes = body.value("notes", "");
        de.totp = body.value("totp", "");
        de.folderId = body.value("folderId", "");
        de.favorite = body.value("favorite", false);
        if (VBCrypto::save_entry(de, true)) {
            VBCrypto::refresh_vault(); trigger_auto_backup();
            send_json(res, {{"success", true}, {"id", de.id}});
        } else {
            send_error(res, 500, "Failed to save entry");
        }
    });

    // --- Update entry ---
    svr.Put(R"(/api/vaultbox/entry/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::string id = req.matches[1].str();
        auto body = parse_body(req);
        DecryptedEntry de;
        de.id = id;
        de.type = body.value("type", 1);
        de.name = body.value("name", "");
        de.username = body.value("username", "");
        de.password = body.value("password", "");
        de.uri = body.value("uri", "");
        de.notes = body.value("notes", "");
        de.totp = body.value("totp", "");
        de.folderId = body.value("folderId", "");
        de.favorite = body.value("favorite", false);
        if (VBCrypto::save_entry(de, false)) {
            VBCrypto::refresh_vault(); trigger_auto_backup();
            send_json(res, {{"success", true}});
        } else {
            send_error(res, 500, "Failed to update entry");
        }
    });

    // --- Delete entry ---
    svr.Delete(R"(/api/vaultbox/entry/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::string id = req.matches[1].str();
        if (VBCrypto::delete_entry(id)) {
            VBCrypto::refresh_vault(); trigger_auto_backup();
            send_json(res, {{"success", true}});
        } else {
            send_error(res, 500, "Failed to delete entry");
        }
    });

    // --- Create folder ---
    svr.Post("/api/vaultbox/folder", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        auto body = parse_body(req);
        std::string name = body.value("name", "");
        if (name.empty()) { send_error(res, 400, "Folder name required"); return; }
        std::string fid = VBCrypto::save_folder(name, "");
        if (!fid.empty()) {
            VBCrypto::refresh_vault(); trigger_auto_backup();
            send_json(res, {{"success", true}, {"id", fid}});
        } else {
            send_error(res, 500, "Failed to create folder");
        }
    });

    // --- Rename folder ---
    svr.Put(R"(/api/vaultbox/folder/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::string id = req.matches[1].str();
        auto body = parse_body(req);
        std::string name = body.value("name", "");
        if (name.empty()) { send_error(res, 400, "Folder name required"); return; }
        VBCrypto::save_folder(name, id);
        VBCrypto::refresh_vault(); trigger_auto_backup();
        send_json(res, {{"success", true}});
    });

    // --- Delete folder ---
    svr.Delete(R"(/api/vaultbox/folder/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::string id = req.matches[1].str();
        VBCrypto::delete_folder(id);
        VBCrypto::refresh_vault(); trigger_auto_backup();
        send_json(res, {{"success", true}});
    });

    // --- Password generator ---
    svr.Post("/api/vaultbox/generate", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        VBPassGen::PassGenOptions opts;
        opts.length = body.value("length", 20);
        opts.upper = body.value("upper", true);
        opts.lower = body.value("lower", true);
        opts.digits = body.value("digits", true);
        opts.symbols = body.value("symbols", true);
        opts.ambiguous = body.value("ambiguous", false);
        std::string pw = VBPassGen::generate_password(opts);
        send_json(res, {{"password", pw}});
    });

    // --- Import ---
    svr.Post(R"(/api/vaultbox/import/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::string type = req.matches[1].str();

        // Write request body to temp file
        auto tempDir = g_data_dir / "temp";
        fs::create_directories(tempDir);
        std::string ext = ".json";
        if (type.find("csv") != std::string::npos) ext = ".csv";
        else if (type.find("xml") != std::string::npos) ext = ".xml";
        auto tempFile = tempDir / ("import_" + generate_uuid() + ext);
        {
            std::ofstream out(tempFile, std::ios::binary);
            out.write(req.body.data(), req.body.size());
        }

        int count = -1;
        if (type == "bitwarden_json") count = VBImport::import_bitwarden_json(tempFile.string());
        else if (type == "bitwarden_csv") count = VBImport::import_bitwarden_csv(tempFile.string());
        else if (type == "chrome_csv") count = VBImport::import_chrome_csv(tempFile.string());
        else if (type == "keepass_xml") count = VBImport::import_keepass_xml(tempFile.string());

        std::error_code ec;
        fs::remove(tempFile, ec);

        if (count >= 0) {
            send_json(res, {{"success", true}, {"count", count}});
        } else if (count == -2) {
            send_error(res, 400, "Encrypted Bitwarden exports are not supported. Re-export unencrypted and try again.");
        } else {
            send_error(res, 400, "Import failed or unsupported format");
        }
    });

    // --- Export ---
    svr.Get(R"(/api/vaultbox/export/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::string type = req.matches[1].str();

        auto tempDir = g_data_dir / "temp";
        fs::create_directories(tempDir);
        auto tempFile = tempDir / ("export_" + generate_uuid() + (type == "json" ? ".json" : ".csv"));

        bool ok = false;
        if (type == "json") ok = VBImport::export_bitwarden_json(tempFile.string());
        else if (type == "csv") ok = VBImport::export_csv(tempFile.string());

        if (ok) {
            std::ifstream in(tempFile, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            fs::remove(tempFile);

            std::string ctype = type == "json" ? "application/json" : "text/csv";
            std::string fname = type == "json" ? "vaultbox-export.json" : "vaultbox-export.csv";
            res.set_header("Content-Disposition", "attachment; filename=\"" + fname + "\"");
            res.set_content(content, ctype);
        } else {
            fs::remove(tempFile);
            send_error(res, 500, "Export failed");
        }
    });

    // --- Start at login ---
    svr.Get("/api/vaultbox/startup", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"enabled", get_startup_enabled()}});
    });
    svr.Post("/api/vaultbox/startup", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        bool enable = body.value("enabled", false);
        bool ok = set_startup_enabled(enable);
        send_json(res, {{"success", ok}, {"enabled", get_startup_enabled()}});
    });

    // --- Security Settings ---
    svr.Get("/api/vaultbox/security", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {
            {"autoLockMinutes", get_autolock_minutes()},
            {"clipboardClearSeconds", get_clipboard_clear_seconds()}
        });
    });
    svr.Post("/api/vaultbox/security", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        if (body.contains("autoLockMinutes") && body["autoLockMinutes"].is_number()) {
            int v = body["autoLockMinutes"].get<int>();
            if (v < 0) v = 0;
            if (v > 24 * 60) v = 24 * 60; // cap at 24 hours
            set_autolock_minutes(v);
        }
        if (body.contains("clipboardClearSeconds") && body["clipboardClearSeconds"].is_number()) {
            int v = body["clipboardClearSeconds"].get<int>();
            if (v < 0) v = 0;
            if (v > 3600) v = 3600; // cap at 1 hour
            set_clipboard_clear_seconds(v);
        }
        send_json(res, {
            {"autoLockMinutes", get_autolock_minutes()},
            {"clipboardClearSeconds", get_clipboard_clear_seconds()}
        });
    });

    // --- Cloud Backup ---
    svr.Get("/api/vaultbox/backup", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {
            {"path", get_backup_path()},
            {"autoBackup", get_auto_backup()},
            {"lastBackup", get_last_backup_time()}
        });
    });
    svr.Post("/api/vaultbox/backup/configure", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string path = body.value("path", "");
        set_backup_path(path);
        if (body.contains("autoBackup")) set_auto_backup(body.value("autoBackup", false));
        send_json(res, {
            {"success", true},
            {"path", get_backup_path()},
            {"autoBackup", get_auto_backup()}
        });
    });
    svr.Post("/api/vaultbox/backup/now", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, do_backup_now());
    });
    svr.Post("/api/vaultbox/backup/restore", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, do_restore_from_backup());
    });
    svr.Post("/api/vaultbox/backup/browse", [](const httplib::Request&, httplib::Response& res) {
        // Return common cloud sync folder paths that exist on this system
        json folders = json::array();
        const char* userprofile = getenv("USERPROFILE");
        if (!userprofile) { send_json(res, {{"folders", folders}}); return; }
        std::string home(userprofile);
        struct { const char* name; std::vector<std::string> paths; } providers[] = {
            {"Google Drive", {home + "\\Google Drive", home + "\\My Drive", home + "\\Google Drive\\My Drive"}},
            {"OneDrive", {home + "\\OneDrive", home + "\\OneDrive - Personal"}},
            {"Dropbox", {home + "\\Dropbox"}},
            {"iCloud Drive", {home + "\\iCloudDrive"}},
        };
        for (auto& p : providers) {
            for (auto& dir : p.paths) {
                if (fs::exists(dir) && fs::is_directory(dir)) {
                    std::string backupDir = dir + "\\VaultBox";
                    folders.push_back({{"name", p.name}, {"path", backupDir}});
                    break;
                }
            }
        }
        send_json(res, {{"folders", folders}});
    });

    // --- Multi-Vault ---
    svr.Get("/api/vaultbox/vaults", [](const httplib::Request&, httplib::Response& res) {
        json vaults = json::array();
        for (auto& entry : fs::directory_iterator(g_data_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".db") {
                std::string name = entry.path().stem().string();
                bool active = (entry.path() == g_db_path);
                auto sz = fs::file_size(entry.path());
                vaults.push_back({{"name", name}, {"file", entry.path().filename().string()}, {"active", active}, {"size", sz}});
            }
        }
        send_json(res, {{"vaults", vaults}, {"dataDir", g_data_dir.string()}});
    });
    svr.Post("/api/vaultbox/vaults/switch", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string file = body.value("file", "");
        if (file.empty() || file.find("..") != std::string::npos
            || file.find('/') != std::string::npos || file.find('\\') != std::string::npos
            || file.find(':') != std::string::npos || file.find('\0') != std::string::npos) {
            send_error(res, 400, "Invalid vault file name"); return;
        }
        fs::path newPath = g_data_dir / file;
        if (!newPath.has_extension() || newPath.extension() != ".db") newPath += ".db";
        // Confirm the resolved path is still inside g_data_dir (defence against traversal).
        std::error_code ec;
        auto absNew = fs::weakly_canonical(newPath, ec);
        auto absDir = fs::weakly_canonical(g_data_dir, ec);
        if (ec || absNew.string().rfind(absDir.string(), 0) != 0) {
            send_error(res, 400, "Invalid vault path"); return;
        }
        try {
            std::lock_guard<std::mutex> lk(g_vault_switch_mtx);
            g_vault.clear();
            g_db_path = newPath;
            init_db(); // Create tables if new vault
        } catch (const std::exception& e) {
            send_error(res, 500, std::string("Failed to switch vault: ") + e.what()); return;
        }
        vb_log("Switched to vault: " + newPath.filename().string());
        send_json(res, {{"success", true}, {"file", newPath.filename().string()}});
    });
    svr.Post("/api/vaultbox/vaults/create", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string name = body.value("name", "");
        if (name.empty()) { send_error(res, 400, "Vault name required"); return; }
        // Sanitize name - allow only filesystem-safe ASCII identifier chars.
        std::string safe;
        for (char c : name) { if (isalnum((unsigned char)c) || c == '-' || c == '_') safe += c; }
        if (safe.empty() || safe.size() > 64) {
            send_error(res, 400, "Vault name must be 1-64 letters/digits/'-'/'_' characters"); return;
        }
        fs::path newPath = g_data_dir / (safe + ".db");
        if (fs::exists(newPath)) { send_error(res, 400, "Vault already exists"); return; }
        try {
            std::lock_guard<std::mutex> lk(g_vault_switch_mtx);
            g_vault.clear();
            g_db_path = newPath;
            init_db();
        } catch (const std::exception& e) {
            send_error(res, 500, std::string("Failed to create vault: ") + e.what()); return;
        }
        vb_log("Created new vault: " + newPath.filename().string());
        send_json(res, {{"success", true}, {"file", newPath.filename().string()}});
    });

    // --- Update Check ---
    svr.Get("/api/vaultbox/update-check", [](const httplib::Request&, httplib::Response& res) {
        // Return current version; the SPA JS will fetch GitHub releases API directly
        send_json(res, {{"currentVersion", APP_VERSION}});
    });

    // --- Launch URI (fallback for non-WebView2 contexts) ---
    svr.Post("/api/vaultbox/launch", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string uri = body.value("uri", "");
        if (!uri.empty() && (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0)) {
            std::wstring wuri = to_wstr(uri);
            ShellExecuteW(nullptr, L"open", wuri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            send_json(res, {{"success", true}});
        } else {
            send_error(res, 400, "Only http:// and https:// URIs are allowed");
        }
    });

    // --- Log stream ---
    svr.Get("/api/vaultbox/logs", [](const httplib::Request&, httplib::Response& res) {
        json logs = json::array();
        std::lock_guard<std::mutex> lk(g_log_mtx);
        while (!g_log_queue.empty()) {
            logs.push_back(g_log_queue.front());
            g_log_queue.pop();
        }
        send_json(res, {{"logs", logs}});
    });

    // --- Prelogin ---
    svr.Post("/accounts/prelogin", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        DB db;
        auto rows = db.query("SELECT kdf, kdf_iterations, kdf_memory, kdf_parallelism FROM accounts WHERE email=?", {email});
        if (!rows.empty()) {
            json resp = {{"kdf", to_int(rows[0]["kdf"], 0)}, {"kdfIterations", to_int(rows[0]["kdf_iterations"], 600000)}};
            if (!rows[0]["kdf_memory"].is_null() && !(rows[0]["kdf_memory"].is_string() && rows[0]["kdf_memory"].get<std::string>().empty()))
                resp["kdfMemory"] = to_int(rows[0]["kdf_memory"], 64);
            if (!rows[0]["kdf_parallelism"].is_null() && !(rows[0]["kdf_parallelism"].is_string() && rows[0]["kdf_parallelism"].get<std::string>().empty()))
                resp["kdfParallelism"] = to_int(rows[0]["kdf_parallelism"], 4);
            send_json(res, resp);
        } else {
            send_json(res, {{"kdf",0}, {"kdfIterations",600000}});
        }
    });

    // --- Registration ---
    svr.Post("/accounts/register/send-verification-email", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        std::string token = base64url_encode("vaultbox-verify:" + email + ":" + generate_hex(16));
        send_json(res, json(token));
    });

    // Safely extract an optional integer from a JSON body. Returns "" when the
    // key is missing / null / non-numeric / non-numeric-string so the resulting
    // SQL bind becomes NULL via NULLIF(?, '').
    auto opt_int_str = [](const json& body, const char* key) -> std::string {
        if (!body.contains(key) || body[key].is_null()) return "";
        try {
            return std::to_string(to_int(body[key], 0));
        } catch (...) {
            return "";
        }
    };

    svr.Post("/accounts/register/finish", [opt_int_str](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        std::string hash = body.value("masterPasswordHash", "");
        if (email.empty() || hash.empty()) { send_error(res, 400, "Email and master password are required"); return; }
        // Basic shape check - reject obvious garbage rather than writing it to the DB.
        if (email.size() > 320 || email.find('@') == std::string::npos) {
            send_error(res, 400, "Invalid email address"); return;
        }
        if (hash.size() < 20 || hash.size() > 4096) {
            send_error(res, 400, "Invalid master password hash"); return;
        }
        std::string uid = generate_uuid(), stamp = generate_uuid(), now = utcnow();
        auto keys = body.value("userAsymmetricKeys", json::object());
        DB db;
        if (!db.query("SELECT id FROM accounts WHERE email=?", {email}).empty()) {
            send_error(res, 400, "A vault already exists. Log in with your master password, or delete vault.db to start fresh."); return;
        }
        int kdf = to_int(body.value("kdf", 0), 0);
        int iters = to_int(body.value("kdfIterations", 600000), 600000);
        if (kdf == 0 && iters < 100000) { send_error(res, 400, "kdfIterations too low"); return; }
        db.run(R"(INSERT INTO accounts (id, email, master_password_hash, master_password_hint,
            security_stamp, key, public_key, encrypted_private_key,
            kdf, kdf_iterations, kdf_memory, kdf_parallelism, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULLIF(?,''), NULLIF(?,''), ?, ?))",
            {uid, email, hash, body.value("masterPasswordHint", ""),
             stamp, body.value("userSymmetricKey", ""),
             keys.value("publicKey", ""), keys.value("encryptedPrivateKey", ""),
             std::to_string(kdf), std::to_string(iters),
             opt_int_str(body, "kdfMemory"), opt_int_str(body, "kdfParallelism"),
             now, now});
        send_json(res, json::object());
        vb_log("New account registered: " + email);
    });

    // Legacy registration
    svr.Post("/accounts/register", [opt_int_str](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        std::string hash = body.value("masterPasswordHash", "");
        if (email.empty() || hash.empty()) { send_error(res, 400, "Email and master password are required"); return; }
        if (email.size() > 320 || email.find('@') == std::string::npos) {
            send_error(res, 400, "Invalid email address"); return;
        }
        if (hash.size() < 20 || hash.size() > 4096) {
            send_error(res, 400, "Invalid master password hash"); return;
        }
        std::string uid = generate_uuid(), stamp = generate_uuid(), now = utcnow();
        auto keys = body.value("keys", json::object());
        DB db;
        if (!db.query("SELECT id FROM accounts WHERE email=?", {email}).empty()) {
            send_error(res, 400, "A vault already exists. Log in with your master password, or delete vault.db to start fresh."); return;
        }
        int kdf = to_int(body.value("kdf", 0), 0);
        int iters = to_int(body.value("kdfIterations", 600000), 600000);
        if (kdf == 0 && iters < 100000) { send_error(res, 400, "kdfIterations too low"); return; }
        db.run(R"(INSERT INTO accounts (id, email, master_password_hash, master_password_hint,
            security_stamp, key, public_key, encrypted_private_key,
            kdf, kdf_iterations, kdf_memory, kdf_parallelism, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULLIF(?,''), NULLIF(?,''), ?, ?))",
            {uid, email, hash, body.value("masterPasswordHint", ""),
             stamp, body.value("key", ""),
             keys.value("publicKey", ""), keys.value("encryptedPrivateKey", ""),
             std::to_string(kdf), std::to_string(iters),
             opt_int_str(body, "kdfMemory"), opt_int_str(body, "kdfParallelism"),
             now, now});
        send_json(res, json::object());
        vb_log("New account registered (legacy): " + email);
    });

    // --- Login (Token) ---
    svr.Post("/connect/token", [](const httplib::Request& req, httplib::Response& res) {
        std::map<std::string, std::string> data;
        auto ct = req.get_header_value("Content-Type");
        if (ct.find("application/x-www-form-urlencoded") != std::string::npos) {
            data = parse_form(req.body);
        } else {
            auto j = parse_body(req);
            for (auto& [k, v] : j.items()) data[k] = v.is_string() ? v.get<std::string>() : v.dump();
        }
        std::string grant = data["grant_type"];
        json user;
        if (grant == "refresh_token") {
            DB db;
            std::string rt = data["refresh_token"];
            auto trows = db.query("SELECT user_id FROM tokens WHERE refresh_token=?", {rt});
            if (trows.empty()) { send_error(res, 400, "Invalid refresh token"); return; }
            auto urows = db.query("SELECT * FROM accounts WHERE id=?", {trows[0]["user_id"].get<std::string>()});
            if (urows.empty()) { send_error(res, 400, "User not found"); return; }
            user = urows[0];
            // Rotate: delete the presented refresh token before issuing a new pair.
            // Prevents unbounded growth of the tokens table and limits replay window.
            db.run("DELETE FROM tokens WHERE refresh_token=?", {rt});
        } else if (grant == "password") {
            std::string email = to_lower(trim(data["username"]));
            std::string pw = data["password"];
            if (email.empty() || pw.empty()) { send_error(res, 400, "Email and password required"); return; }
            DB db;
            auto rows = db.query("SELECT * FROM accounts WHERE email=?", {email});
            if (rows.empty()) { send_error(res, 400, "Invalid email or password"); return; }
            user = rows[0];
            if (!constant_time_eq(user["master_password_hash"].get<std::string>(), pw)) {
                send_error(res, 400, "Invalid email or password"); return;
            }
        } else {
            send_error(res, 400, "Unsupported grant_type: " + grant); return;
        }
        auto safe = [&](const char* k) -> json {
            return (user.contains(k) && !user[k].is_null()) ? user[k] : json(nullptr);
        };
        auto safe_int = [&](const char* k, int fallback = 0) -> json {
            if (!user.contains(k) || user[k].is_null()) return json(nullptr);
            if (user[k].is_string() && user[k].get<std::string>().empty()) return json(nullptr);
            return json(to_int(user[k], fallback));
        };
        send_json(res, {
            {"access_token", create_access_token(user["id"].get<std::string>(), user["email"].get<std::string>())},
            {"expires_in", TOKEN_EXPIRY_HOURS * 3600},
            {"token_type", "Bearer"},
            {"refresh_token", create_refresh_token(user["id"].get<std::string>())},
            {"Key", safe("key")}, {"PrivateKey", safe("encrypted_private_key")},
            {"AccountKeys", build_account_keys(user)},
            {"Kdf", safe_int("kdf")}, {"KdfIterations", safe_int("kdf_iterations")},
            {"KdfMemory", safe_int("kdf_memory")}, {"KdfParallelism", safe_int("kdf_parallelism")},
            {"ResetMasterPassword", false}, {"ForcePasswordReset", false},
            {"MasterPasswordPolicy", nullptr},
            {"UserDecryptionOptions", {{"HasMasterPassword", true}}},
            {"scope", "api offline_access"},
        });
        vb_log("Login: " + user["email"].get<std::string>());
    });

    // --- Profile ---
    svr.Get("/accounts/profile", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        send_json(res, build_profile(user));
    });
    svr.Put("/accounts/profile", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        std::string name = body.value("name", user.value("name", std::string("")));
        std::string culture = body.value("culture", user.value("culture", std::string("en-US")));
        DB db;
        db.run("UPDATE accounts SET name=?, culture=?, updated_at=? WHERE id=?",
            {name, culture, utcnow(), user["id"].get<std::string>()});
        auto rows = db.query("SELECT * FROM accounts WHERE id=?", {user["id"].get<std::string>()});
        if (rows.empty()) { send_error(res, 500, "Failed to update profile"); return; }
        send_json(res, build_profile(rows[0]));
    });

    // --- Keys ---
    svr.Post("/accounts/keys", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        DB db;
        db.run("UPDATE accounts SET public_key=?, encrypted_private_key=?, updated_at=? WHERE id=?",
            {body.value("publicKey",""), body.value("encryptedPrivateKey",""), utcnow(), user["id"].get<std::string>()});
        res.status = 200;
    });
    svr.Post("/accounts/key", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        DB db;
        db.run("UPDATE accounts SET key=?, updated_at=? WHERE id=?",
            {body.value("key",""), utcnow(), user["id"].get<std::string>()});
        res.status = 200;
    });

    svr.Post("/accounts/password", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        if (!constant_time_eq(user["master_password_hash"].get<std::string>(), body.value("masterPasswordHash",""))) {
            send_error(res, 400, "Invalid current master password"); return;
        }
        DB db;
        db.run("UPDATE accounts SET master_password_hash=?, key=?, security_stamp=?, updated_at=? WHERE id=?",
            {body.value("newMasterPasswordHash",""), body.value("key",""), generate_uuid(), utcnow(), user["id"].get<std::string>()});
        res.status = 200;
    });

    svr.Post("/accounts/kdf", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        int kdf = body.value("kdf", (int)user.value("kdf", (int64_t)0));
        int iters = body.value("kdfIterations", (int)user.value("kdf_iterations", (int64_t)600000));
        // Refuse dangerously weak KDF parameters. PBKDF2 (kdf=0) lower bound mirrors
        // Bitwarden server-side validation; Argon2id (kdf=1) iterations are small
        // numbers but must not be zero/negative.
        if (kdf == 0 && iters < 100000) { send_error(res, 400, "kdfIterations too low"); return; }
        if (kdf == 1 && (iters < 2 || iters > 100)) { send_error(res, 400, "kdfIterations out of range"); return; }
        std::string uid = user["id"].get<std::string>();
        DB db;
        db.run("UPDATE accounts SET kdf=?, kdf_iterations=?, kdf_memory=NULLIF(?,''), kdf_parallelism=NULLIF(?,''), key=?, master_password_hash=?, updated_at=? WHERE id=?",
            {std::to_string(kdf), std::to_string(iters),
             body.contains("kdfMemory") && !body["kdfMemory"].is_null() ? std::to_string(body["kdfMemory"].get<int>()) : "",
             body.contains("kdfParallelism") && !body["kdfParallelism"].is_null() ? std::to_string(body["kdfParallelism"].get<int>()) : "",
             body.value("key", user.value("key", std::string(""))),
             body.value("newMasterPasswordHash", user["master_password_hash"].get<std::string>()),
             utcnow(), uid});
        res.status = 200;
    });

    svr.Post("/accounts/verify-password", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        if (constant_time_eq(user["master_password_hash"].get<std::string>(), body.value("masterPasswordHash","")))
            res.status = 200;
        else send_error(res, 400, "Invalid password");
    });

    svr.Get("/accounts/revision-date", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        std::string uid = user["id"].get<std::string>();
        DB db;
        auto rows = db.query(R"(SELECT MAX(ts) as latest FROM (
            SELECT updated_at as ts FROM accounts WHERE id=?
            UNION ALL SELECT MAX(updated_at) FROM ciphers WHERE user_id=?
            UNION ALL SELECT MAX(updated_at) FROM folders WHERE user_id=?
        ))", {uid, uid, uid});
        std::string ts = (!rows.empty() && rows[0]["latest"].is_string()) ? rows[0]["latest"].get<std::string>() : user["updated_at"].get<std::string>();
        res.set_content(std::to_string(iso_to_ms(ts)), "text/plain");
    });

    // --- Sync ---
    svr.Get("/sync", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        std::string uid = user["id"].get<std::string>();
        DB db;
        auto crow = db.query("SELECT * FROM ciphers WHERE user_id=? ORDER BY created_at", {uid});
        auto frow = db.query("SELECT * FROM folders WHERE user_id=? ORDER BY created_at", {uid});
        json ciphers = json::array(), folders = json::array();
        for (auto& r : crow) ciphers.push_back(build_cipher(r));
        for (auto& r : frow) folders.push_back(build_folder(r));
        send_json(res, {
            {"object","sync"}, {"profile", build_profile(user)},
            {"folders", folders}, {"collections", json::array()}, {"ciphers", ciphers},
            {"domains", {{"object","domains"}, {"equivalentDomains",json::array()}, {"globalEquivalentDomains",json::array()}}},
            {"policies", json::array()}, {"sends", json::array()},
        });
    });

    // --- Ciphers ---
    svr.Post("/ciphers/create", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        auto cipher = body.contains("cipher") ? body["cipher"] : body;
        send_json(res, upsert_cipher(user["id"].get<std::string>(), "", cipher));
    });

    svr.Post("/ciphers/purge", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        if (!constant_time_eq(user["master_password_hash"].get<std::string>(), body.value("masterPasswordHash",""))) {
            send_error(res, 400, "Invalid password"); return;
        }
        DB db;
        db.run("DELETE FROM ciphers WHERE user_id=?", {user["id"].get<std::string>()});
        res.status = 200;
    });

    svr.Post("/ciphers/import", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        std::string uid = user["id"].get<std::string>();
        std::string now = utcnow();
        auto folders_data = body.value("folders", json::array());
        auto ciphers_data = body.value("ciphers", json::array());
        auto rels = body.value("folderRelationships", json::array());
        std::map<int, std::string> folder_map;
        std::map<int, int> cfmap;
        for (auto& r : rels) {
            int k = r.contains("key") && r["key"].is_number() ? r["key"].get<int>() : r.value("Key", -1);
            int v = r.contains("value") && r["value"].is_number() ? r["value"].get<int>() : r.value("Value", -1);
            if (k >= 0) cfmap[k] = v;
        }

        DB db;
        // Wrap the whole import in one transaction: ~100x faster for large
        // imports (no fsync per row) and atomic on failure - a partial crash
        // won't leave the user with half a vault.
        db.exec("BEGIN IMMEDIATE");
        try {
            for (int i = 0; i < (int)folders_data.size(); i++) {
                std::string fid = generate_uuid();
                folder_map[i] = fid;
                db.run("INSERT INTO folders (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
                    {fid, uid, folders_data[i].value("name",""), now, now});
            }
            for (int i = 0; i < (int)ciphers_data.size(); i++) {
                auto& c = ciphers_data[i];
                std::string cid = generate_uuid();
                std::string fid = "";
                if (cfmap.count(i) && folder_map.count(cfmap[i])) fid = folder_map[cfmap[i]];
                std::string ct = std::to_string(c.value("type", 1));
                std::string fav = c.value("favorite", false) ? "1" : "0";
                std::string rep = std::to_string(c.value("reprompt", 0));
                db.run("INSERT INTO ciphers (id, user_id, folder_id, organization_id, type, data, favorite, reprompt, created_at, updated_at) VALUES (?, ?, NULLIF(?,''), NULL, ?, ?, ?, ?, ?, ?)",
                    {cid, uid, fid, ct, extract_cipher_data(c).dump(), fav, rep, now, now});
            }
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            send_error(res, 500, "Import failed; no changes were made");
            return;
        }
        res.status = 200;
        vb_log("Import: " + std::to_string(ciphers_data.size()) + " ciphers, " + std::to_string(folders_data.size()) + " folders");
        trigger_auto_backup();
    });

    auto get_cipher_handler = [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        std::string cid = req.matches[1].str();
        DB db;
        auto rows = db.query("SELECT * FROM ciphers WHERE id=? AND user_id=?", {cid, user["id"].get<std::string>()});
        if (rows.empty()) { send_error(res, 404, "Cipher not found"); return; }
        send_json(res, build_cipher(rows[0]));
    };
    svr.Get(R"(/ciphers/([^/]+)/details)", get_cipher_handler);
    svr.Get(R"(/ciphers/([^/]+)/admin)", get_cipher_handler);

    auto soft_delete = [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        std::string now = utcnow();
        DB db;
        db.run("UPDATE ciphers SET deleted_at=?, updated_at=? WHERE id=? AND user_id=?",
            {now, now, req.matches[1].str(), user["id"].get<std::string>()});
        res.status = 200;
    };
    svr.Put(R"(/ciphers/([^/]+)/delete)", soft_delete);
    svr.Post(R"(/ciphers/([^/]+)/delete)", soft_delete);

    svr.Put(R"(/ciphers/([^/]+)/restore)", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        std::string cid = req.matches[1].str(), uid = user["id"].get<std::string>();
        DB db;
        db.run("UPDATE ciphers SET deleted_at=NULL, updated_at=? WHERE id=? AND user_id=?", {utcnow(), cid, uid});
        auto rows = db.query("SELECT * FROM ciphers WHERE id=? AND user_id=?", {cid, uid});
        if (rows.empty()) { send_error(res, 404, "Cipher not found"); return; }
        send_json(res, build_cipher(rows[0]));
    });

    svr.Put(R"(/ciphers/([^/]+)/partial)", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        std::string cid = req.matches[1].str(), uid = user["id"].get<std::string>();
        std::string fid = body.contains("folderId") && body["folderId"].is_string() ? body["folderId"].get<std::string>() : "";
        std::string fav = body.value("favorite", false) ? "1" : "0";
        DB db;
        db.run("UPDATE ciphers SET folder_id=NULLIF(?,''), favorite=?, updated_at=? WHERE id=? AND user_id=?",
            {fid, fav, utcnow(), cid, uid});
        auto rows = db.query("SELECT * FROM ciphers WHERE id=? AND user_id=?", {cid, uid});
        if (rows.empty()) { send_error(res, 404, "Cipher not found"); return; }
        send_json(res, build_cipher(rows[0]));
    });

    svr.Put(R"(/ciphers/([^/]+)/favorite)", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        DB db;
        db.run("UPDATE ciphers SET favorite=?, updated_at=? WHERE id=? AND user_id=?",
            {body.value("favorite",false) ? "1" : "0", utcnow(), req.matches[1].str(), user["id"].get<std::string>()});
        res.status = 200;
    });

    auto coll_stub = [](const httplib::Request&, httplib::Response& res) { res.status = 200; };
    svr.Post(R"(/ciphers/([^/]+)/collections)", coll_stub);
    svr.Post(R"(/ciphers/([^/]+)/collections-admin)", coll_stub);

    svr.Post("/ciphers", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        send_json(res, upsert_cipher(user["id"].get<std::string>(), "", parse_body(req)));
    });

    svr.Get(R"(/ciphers/([^/]+))", get_cipher_handler);

    auto update_cipher = [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        send_json(res, upsert_cipher(user["id"].get<std::string>(), req.matches[1].str(), parse_body(req)));
    };
    svr.Put(R"(/ciphers/([^/]+))", update_cipher);

    svr.Delete(R"(/ciphers/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        DB db;
        db.run("DELETE FROM ciphers WHERE id=? AND user_id=?", {req.matches[1].str(), user["id"].get<std::string>()});
        res.status = 200;
    });

    // --- Folders ---
    svr.Post("/folders", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        std::string fid = generate_uuid(), now = utcnow();
        DB db;
        db.run("INSERT INTO folders (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
            {fid, user["id"].get<std::string>(), body.value("name",""), now, now});
        auto rows = db.query("SELECT * FROM folders WHERE id=?", {fid});
        if (rows.empty()) { send_error(res, 500, "Failed to create folder"); return; }
        send_json(res, build_folder(rows[0]));
    });

    svr.Put(R"(/folders/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        std::string fid = req.matches[1].str(), uid = user["id"].get<std::string>();
        DB db;
        db.run("UPDATE folders SET name=?, updated_at=? WHERE id=? AND user_id=?",
            {body.value("name",""), utcnow(), fid, uid});
        auto rows = db.query("SELECT * FROM folders WHERE id=? AND user_id=?", {fid, uid});
        if (rows.empty()) { send_error(res, 404, "Folder not found"); return; }
        send_json(res, build_folder(rows[0]));
    });

    svr.Delete(R"(/folders/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        std::string fid = req.matches[1].str(), uid = user["id"].get<std::string>();
        DB db;
        db.exec("BEGIN IMMEDIATE");
        try {
            db.run("UPDATE ciphers SET folder_id=NULL, updated_at=? WHERE folder_id=? AND user_id=?", {utcnow(), fid, uid});
            db.run("DELETE FROM folders WHERE id=? AND user_id=?", {fid, uid});
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            send_error(res, 500, "Failed to delete folder");
            return;
        }
        res.status = 200;
    });

    // --- Stubs ---
    auto empty_list = [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"object","list"}, {"data",json::array()}, {"continuationToken",nullptr}});
    };
    svr.Get("/organizations", empty_list);
    svr.Get("/collections", empty_list);
    svr.Get("/sends", empty_list);
    svr.Get("/auth-requests", empty_list);

    svr.Get(R"(/organizations/([^/]+)/auto-enroll-status)", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"resetPasswordEnabled", false}});
    });

    svr.Get("/settings/domains", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"object","domains"}, {"equivalentDomains",json::array()}, {"globalEquivalentDomains",json::array()}});
    });
    svr.Put("/settings/domains", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

    svr.Get(R"(/devices/identifier/([^/]+)/type/([^/]+))", [](const httplib::Request&, httplib::Response& res) {
        send_error(res, 404, "Device not found");
    });

    svr.Get("/config", [](const httplib::Request&, httplib::Response& res) {
        std::string base = std::string("http://") + HOST + ":" + std::to_string(PORT);
        send_json(res, {
            {"object","config"}, {"version","2024.1.0"}, {"gitHash","vaultbox"},
            {"server", {{"name","VaultBox"}, {"url", base}}},
            {"environment", {{"cloudRegion",nullptr}, {"vault",base}, {"api",base},
                {"identity",base}, {"notifications",nullptr}, {"sso",nullptr}}},
            {"featureStates", json::object()},
        });
    });

    svr.Post("/accounts/api-key", [](const httplib::Request& req, httplib::Response& res) {
        if (get_current_user(req, res).is_null()) return;
        send_json(res, {{"apiKey", generate_hex(16)}});
    });
    svr.Post("/accounts/rotate-api-key", [](const httplib::Request& req, httplib::Response& res) {
        if (get_current_user(req, res).is_null()) return;
        send_json(res, {{"apiKey", generate_hex(16)}});
    });

    auto delete_account = [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        if (!constant_time_eq(user["master_password_hash"].get<std::string>(), body.value("masterPasswordHash",""))) {
            send_error(res, 400, "Invalid password"); return;
        }
        std::string uid = user["id"].get<std::string>();
        DB db;
        db.exec("BEGIN IMMEDIATE");
        try {
            db.run("DELETE FROM ciphers WHERE user_id=?", {uid});
            db.run("DELETE FROM folders WHERE user_id=?", {uid});
            db.run("DELETE FROM tokens WHERE user_id=?", {uid});
            db.run("DELETE FROM accounts WHERE id=?", {uid});
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            send_error(res, 500, "Failed to delete account"); return;
        }
        // Clear the in-memory vault state too - the user just asked us to wipe
        // everything, there's nothing left to be unlocked.
        g_vault.clear();
        res.status = 200;
        vb_log("Account deleted: " + user["email"].get<std::string>());
    };
    svr.Post("/accounts/delete", delete_account);
    svr.Delete("/accounts", delete_account);

    svr.Post("/accounts/verify-devices", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    svr.Post("/collect", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    svr.Post("/events/collect", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    svr.Get(R"(/auth-requests/([^/]+))", [](const httplib::Request&, httplib::Response& res) {
        send_error(res, 404, "Not found");
    });

    // --- Exception/Error handler ---
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { send_error(res, 500, e.what()); }
        catch (...) { send_error(res, 500, "Internal server error"); }
    });

    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        // Only silently-swallow 404s on the Bitwarden-compatible API surface,
        // where the Angular extension hits optional endpoints we don't implement
        // (organizations, sends, auth-requests, etc.). Never swallow 404s on
        // /api/vaultbox/* - a typo or removed endpoint in the SPA should surface
        // as a real error so we don't ship a broken UI that silently appears fine.
        if (res.status == 404) {
            const std::string& p = req.path;
            if (p.rfind("/api/vaultbox/", 0) == 0 || p == "/api/vaultbox") return;
            res.status = 200;
            if (req.method == "GET")
                res.set_content(R"({"object":"list","data":[],"continuationToken":null})", "application/json");
            else
                res.set_content("{}", "application/json");
        }
    });
}
