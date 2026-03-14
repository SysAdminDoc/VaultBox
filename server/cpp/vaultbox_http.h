// VaultBox Desktop - HTTP Server (Bitwarden-compatible API + GUI API)
#pragma once
#include "vaultbox_server.h"
#include "vaultbox_db.h"
#include "vaultbox_ui.h"

inline void setup_routes(httplib::Server& svr) {

    // --- CORS ---
    svr.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        auto origin = req.get_header_value("Origin");
        bool allowed = origin.empty()
            || origin.rfind("chrome-extension://", 0) == 0
            || origin.rfind("moz-extension://", 0) == 0
            || origin == "http://127.0.0.1:8787"
            || origin == "http://localhost:8787";
        std::string cors_origin = allowed && !origin.empty() ? origin : "http://127.0.0.1:8787";
        res.set_header("Access-Control-Allow-Origin", cors_origin);
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "*");
        res.set_header("Access-Control-Allow-Credentials", "true");
    });

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    // --- Serve SPA at root ---
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
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
        send_json(res, {{"unlocked", g_vault.unlocked}, {"email", email}, {"version", APP_VERSION}});
    });

    // --- Unlock ---
    svr.Post("/api/vaultbox/unlock", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = body.value("email", "");
        std::string password = body.value("password", "");
        if (email.empty() || password.empty()) {
            send_error(res, 400, "Email and password required"); return;
        }
        if (VBCrypto::unlock_vault(password, email)) {
            send_json(res, {{"success", true}, {"entries", (int)g_vault.entries.size()}, {"folders", (int)g_vault.folders.size()}});
        } else {
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
                {"type", e.type}, {"favorite", e.favorite},
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
        de.folderId = body.value("folderId", "");
        de.favorite = body.value("favorite", false);
        if (VBCrypto::save_entry(de, true)) {
            VBCrypto::refresh_vault();
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
        de.folderId = body.value("folderId", "");
        de.favorite = body.value("favorite", false);
        if (VBCrypto::save_entry(de, false)) {
            VBCrypto::refresh_vault();
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
            VBCrypto::refresh_vault();
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
            VBCrypto::refresh_vault();
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
        VBCrypto::refresh_vault();
        send_json(res, {{"success", true}});
    });

    // --- Delete folder ---
    svr.Delete(R"(/api/vaultbox/folder/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_vault.unlocked) { send_error(res, 401, "Vault is locked"); return; }
        std::string id = req.matches[1].str();
        VBCrypto::delete_folder(id);
        VBCrypto::refresh_vault();
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

        fs::remove(tempFile);

        if (count >= 0) {
            send_json(res, {{"success", true}, {"count", count}});
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

    // --- Launch URI (fallback for non-WebView2 contexts) ---
    svr.Post("/api/vaultbox/launch", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string uri = body.value("uri", "");
        if (!uri.empty()) {
            std::wstring wuri = to_wstr(uri);
            ShellExecuteW(nullptr, L"open", wuri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        send_json(res, {{"success", true}});
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

    svr.Post("/accounts/register/finish", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        std::string hash = body.value("masterPasswordHash", "");
        if (email.empty() || hash.empty()) { send_error(res, 400, "Email and master password are required"); return; }
        std::string uid = generate_uuid(), stamp = generate_uuid(), now = utcnow();
        auto keys = body.value("userAsymmetricKeys", json::object());
        DB db;
        if (!db.query("SELECT id FROM accounts WHERE email=?", {email}).empty()) {
            send_error(res, 400, "A vault already exists. Log in with your master password, or delete vault.db to start fresh."); return;
        }
        db.run(R"(INSERT INTO accounts (id, email, master_password_hash, master_password_hint,
            security_stamp, key, public_key, encrypted_private_key,
            kdf, kdf_iterations, kdf_memory, kdf_parallelism, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
            {uid, email, hash, body.value("masterPasswordHint", ""),
             stamp, body.value("userSymmetricKey", ""),
             keys.value("publicKey", ""), keys.value("encryptedPrivateKey", ""),
             std::to_string(body.value("kdf", 0)), std::to_string(body.value("kdfIterations", 600000)),
             body.contains("kdfMemory") && !body["kdfMemory"].is_null() ? std::to_string(body["kdfMemory"].get<int>()) : "",
             body.contains("kdfParallelism") && !body["kdfParallelism"].is_null() ? std::to_string(body["kdfParallelism"].get<int>()) : "",
             now, now});
        send_json(res, json::object());
        vb_log("New account registered: " + email);
    });

    // Legacy registration
    svr.Post("/accounts/register", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        std::string hash = body.value("masterPasswordHash", "");
        if (email.empty() || hash.empty()) { send_error(res, 400, "Email and master password are required"); return; }
        std::string uid = generate_uuid(), stamp = generate_uuid(), now = utcnow();
        auto keys = body.value("keys", json::object());
        DB db;
        if (!db.query("SELECT id FROM accounts WHERE email=?", {email}).empty()) {
            send_error(res, 400, "A vault already exists. Log in with your master password, or delete vault.db to start fresh."); return;
        }
        db.run(R"(INSERT INTO accounts (id, email, master_password_hash, master_password_hint,
            security_stamp, key, public_key, encrypted_private_key,
            kdf, kdf_iterations, kdf_memory, kdf_parallelism, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULLIF(?,''), NULLIF(?,''), ?, ?))",
            {uid, email, hash, body.value("masterPasswordHint", ""),
             stamp, body.value("key", ""),
             keys.value("publicKey", ""), keys.value("encryptedPrivateKey", ""),
             std::to_string(body.value("kdf", 0)), std::to_string(body.value("kdfIterations", 600000)),
             body.contains("kdfMemory") && !body["kdfMemory"].is_null() ? std::to_string(body["kdfMemory"].get<int>()) : "",
             body.contains("kdfParallelism") && !body["kdfParallelism"].is_null() ? std::to_string(body["kdfParallelism"].get<int>()) : "",
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
            auto trows = db.query("SELECT user_id FROM tokens WHERE refresh_token=?", {data["refresh_token"]});
            if (trows.empty()) { send_error(res, 400, "Invalid refresh token"); return; }
            auto urows = db.query("SELECT * FROM accounts WHERE id=?", {trows[0]["user_id"].get<std::string>()});
            if (urows.empty()) { send_error(res, 400, "User not found"); return; }
            user = urows[0];
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
        std::string uid = user["id"].get<std::string>();
        DB db;
        db.run("UPDATE accounts SET kdf=?, kdf_iterations=?, kdf_memory=NULLIF(?,''), kdf_parallelism=NULLIF(?,''), key=?, master_password_hash=?, updated_at=? WHERE id=?",
            {std::to_string(body.value("kdf", (int)user.value("kdf",(int64_t)0))),
             std::to_string(body.value("kdfIterations", (int)user.value("kdf_iterations",(int64_t)600000))),
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
        DB db;
        for (int i = 0; i < (int)folders_data.size(); i++) {
            std::string fid = generate_uuid();
            folder_map[i] = fid;
            db.run("INSERT INTO folders (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
                {fid, uid, folders_data[i].value("name",""), now, now});
        }
        std::map<int, int> cfmap;
        for (auto& r : rels) {
            int k = r.contains("key") ? r["key"].get<int>() : r.value("Key", -1);
            int v = r.contains("value") ? r["value"].get<int>() : r.value("Value", -1);
            cfmap[k] = v;
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
        res.status = 200;
        vb_log("Import: " + std::to_string(ciphers_data.size()) + " ciphers, " + std::to_string(folders_data.size()) + " folders");
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
        db.run("UPDATE ciphers SET folder_id=NULL, updated_at=? WHERE folder_id=? AND user_id=?", {utcnow(), fid, uid});
        db.run("DELETE FROM folders WHERE id=? AND user_id=?", {fid, uid});
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
        db.run("DELETE FROM ciphers WHERE user_id=?", {uid});
        db.run("DELETE FROM folders WHERE user_id=?", {uid});
        db.run("DELETE FROM tokens WHERE user_id=?", {uid});
        db.run("DELETE FROM accounts WHERE id=?", {uid});
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
        if (res.status == 404) {
            res.status = 200;
            if (req.method == "GET")
                res.set_content(R"({"object":"list","data":[],"continuationToken":null})", "application/json");
            else
                res.set_content("{}", "application/json");
        }
    });
}
