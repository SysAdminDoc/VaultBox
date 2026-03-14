// VaultBox Server v0.3.0 - Native C++ Implementation
// Bitwarden-compatible local API server for offline password management
// Single binary, zero runtime dependencies
// Listens on 127.0.0.1:8787 - never exposed to network

// ============================================================================
// Platform & Linker Setup
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <rpc.h>

#undef DELETE
#undef min
#undef max

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <functional>
#include <random>
#include <algorithm>
#include <atomic>

#include "deps/sqlite3.h"
#include "deps/json.hpp"

// httplib after all Windows headers to avoid macro conflicts
#include "deps/httplib.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// Configuration & Globals
// ============================================================================
static const char* HOST = "127.0.0.1";
static const int PORT = 8787;
static const int TOKEN_EXPIRY_HOURS = 24 * 30;
static const char* SERVER_VERSION = "0.3.0";

static fs::path g_data_dir;
static fs::path g_db_path;
static std::string g_jwt_secret;
static std::atomic<bool> g_shutdown{false};
static httplib::Server* g_server = nullptr;

// Tray icon message
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_OPEN 1001
#define ID_TRAY_QUIT 1002

// ============================================================================
// Utility Functions
// ============================================================================
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string r;
    r.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];
        r += B64[(n >> 18) & 0x3F];
        r += B64[(n >> 12) & 0x3F];
        r += (i + 1 < len) ? B64[(n >> 6) & 0x3F] : '=';
        r += (i + 2 < len) ? B64[n & 0x3F] : '=';
    }
    return r;
}

std::vector<uint8_t> base64_decode(const std::string& s) {
    static int tbl[256];
    static bool init = false;
    if (!init) {
        std::fill(tbl, tbl + 256, -1);
        for (int i = 0; i < 64; i++) tbl[(unsigned char)B64[i]] = i;
        init = true;
    }
    std::vector<uint8_t> r;
    r.reserve(s.size() * 3 / 4);
    int val = 0, bits = -8;
    for (unsigned char c : s) {
        if (tbl[c] == -1) continue;
        val = (val << 6) + tbl[c];
        bits += 6;
        if (bits >= 0) { r.push_back((val >> bits) & 0xFF); bits -= 8; }
    }
    return r;
}

std::string base64url_encode(const uint8_t* data, size_t len) {
    std::string b = base64_encode(data, len);
    for (auto& c : b) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
    while (!b.empty() && b.back() == '=') b.pop_back();
    return b;
}

std::string base64url_encode(const std::string& s) {
    return base64url_encode((const uint8_t*)s.data(), s.size());
}

std::vector<uint8_t> base64url_decode(const std::string& s) {
    std::string b = s;
    for (auto& c : b) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
    while (b.size() % 4) b += '=';
    return base64_decode(b);
}

std::string generate_uuid() {
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR str;
    UuidToStringA(&uuid, &str);
    std::string r((char*)str);
    RpcStringFreeA(&str);
    return r;
}

std::string generate_hex(int bytes = 32) {
    static thread_local std::mt19937 rng(std::random_device{}());
    static const char hex[] = "0123456789abcdef";
    std::string r;
    r.reserve(bytes * 2);
    for (int i = 0; i < bytes; i++) {
        uint8_t b = rng() & 0xFF;
        r += hex[b >> 4];
        r += hex[b & 0xF];
    }
    return r;
}

std::string utcnow() {
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm tm;
    gmtime_s(&tm, &tt);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

int64_t current_timestamp() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t iso_to_ms(const std::string& iso) {
    struct tm tm = {};
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        return (int64_t)_mkgmtime(&tm) * 1000;
    }
    return current_timestamp() * 1000;
}

bool constant_time_eq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile uint8_t r = 0;
    for (size_t i = 0; i < a.size(); i++) r |= (uint8_t)(a[i] ^ b[i]);
    return r == 0;
}

std::string url_decode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v; if (sscanf(s.c_str() + i + 1, "%2x", &v) == 1) { r += (char)v; i += 2; continue; }
        } else if (s[i] == '+') { r += ' '; continue; }
        r += s[i];
    }
    return r;
}

std::map<std::string, std::string> parse_form(const std::string& body) {
    std::map<std::string, std::string> p;
    std::istringstream ss(body);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos)
            p[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
    }
    return p;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

json parse_body(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    try { return json::parse(req.body); } catch (...) { return json::object(); }
}

void send_json(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void send_error(httplib::Response& res, int status, const std::string& msg) {
    res.status = status;
    res.set_content(json({{"detail", msg}}).dump(), "application/json");
}

// ============================================================================
// HMAC-SHA256 (Windows BCrypt)
// ============================================================================
std::vector<uint8_t> hmac_sha256(const std::string& key, const std::string& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::vector<uint8_t> result;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))
        return result;

    DWORD hashLen = 0, tmp = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashLen, sizeof(hashLen), &tmp, 0);
    result.resize(hashLen);

    DWORD objLen = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&objLen, sizeof(objLen), &tmp, 0);
    std::vector<uint8_t> obj(objLen);

    BCryptCreateHash(hAlg, &hHash, obj.data(), objLen, (PBYTE)key.data(), (ULONG)key.size(), 0);
    BCryptHashData(hHash, (PBYTE)data.data(), (ULONG)data.size(), 0);
    BCryptFinishHash(hHash, result.data(), hashLen, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}

// ============================================================================
// JWT
// ============================================================================
std::string jwt_encode(const json& payload) {
    std::string h = base64url_encode(json({{"alg","HS256"},{"typ","JWT"}}).dump());
    std::string p = base64url_encode(payload.dump());
    std::string msg = h + "." + p;
    auto sig = hmac_sha256(g_jwt_secret, msg);
    return msg + "." + base64url_encode(sig.data(), sig.size());
}

json jwt_decode(const std::string& token) {
    auto d1 = token.find('.');
    auto d2 = token.find('.', d1 + 1);
    if (d1 == std::string::npos || d2 == std::string::npos) throw std::runtime_error("Invalid token");

    std::string msg = token.substr(0, d2);
    auto expected = hmac_sha256(g_jwt_secret, msg);
    auto actual = base64url_decode(token.substr(d2 + 1));

    if (expected.size() != actual.size()) throw std::runtime_error("Invalid signature");
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < expected.size(); i++) diff |= expected[i] ^ actual[i];
    if (diff) throw std::runtime_error("Invalid signature");

    auto pb = base64url_decode(token.substr(d1 + 1, d2 - d1 - 1));
    json payload = json::parse(std::string(pb.begin(), pb.end()));

    if (payload.contains("exp") && current_timestamp() > payload["exp"].get<int64_t>())
        throw std::runtime_error("Token expired");

    return payload;
}

std::string create_access_token(const std::string& uid, const std::string& email) {
    int64_t now = current_timestamp();
    return jwt_encode({
        {"sub", uid}, {"email", email}, {"name", email.substr(0, email.find('@'))},
        {"premium", true}, {"email_verified", true}, {"iss", "vaultbox|local"},
        {"iat", now}, {"nbf", now}, {"exp", now + TOKEN_EXPIRY_HOURS * 3600},
        {"scope", json::array({"api", "offline_access"})},
        {"amr", json::array({"Application"})},
    });
}

// ============================================================================
// Database
// ============================================================================
class DB {
    sqlite3* db_ = nullptr;
public:
    DB() {
        sqlite3_open(g_db_path.string().c_str(), &db_);
        sqlite3_busy_timeout(db_, 5000);
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
    }
    ~DB() { if (db_) sqlite3_close(db_); }

    void exec(const char* sql) { sqlite3_exec(db_, sql, nullptr, nullptr, nullptr); }

    std::vector<json> query(const std::string& sql, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* s = nullptr;
        std::vector<json> rows;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr) != SQLITE_OK) return rows;
        for (size_t i = 0; i < params.size(); i++)
            sqlite3_bind_text(s, (int)i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);

        int cols = sqlite3_column_count(s);
        while (sqlite3_step(s) == SQLITE_ROW) {
            json row;
            for (int c = 0; c < cols; c++) {
                const char* name = sqlite3_column_name(s, c);
                int type = sqlite3_column_type(s, c);
                if (type == SQLITE_NULL) row[name] = nullptr;
                else if (type == SQLITE_INTEGER) row[name] = sqlite3_column_int64(s, c);
                else row[name] = std::string((const char*)sqlite3_column_text(s, c));
            }
            rows.push_back(std::move(row));
        }
        sqlite3_finalize(s);
        return rows;
    }

    void run(const std::string& sql, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr) != SQLITE_OK) return;
        for (size_t i = 0; i < params.size(); i++)
            sqlite3_bind_text(s, (int)i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
};

void init_db() {
    fs::create_directories(g_data_dir);

    DB db;
    db.exec(R"(
        CREATE TABLE IF NOT EXISTS config (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS accounts (
            id TEXT PRIMARY KEY, email TEXT UNIQUE NOT NULL, name TEXT DEFAULT '',
            master_password_hash TEXT NOT NULL, master_password_hint TEXT DEFAULT '',
            security_stamp TEXT NOT NULL, key TEXT DEFAULT '',
            public_key TEXT DEFAULT '', encrypted_private_key TEXT DEFAULT '',
            kdf INTEGER DEFAULT 1, kdf_iterations INTEGER DEFAULT 600000,
            kdf_memory INTEGER, kdf_parallelism INTEGER,
            culture TEXT DEFAULT 'en-US', created_at TEXT NOT NULL, updated_at TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS ciphers (
            id TEXT PRIMARY KEY, user_id TEXT NOT NULL, folder_id TEXT,
            organization_id TEXT, type INTEGER NOT NULL, data TEXT NOT NULL,
            favorite INTEGER DEFAULT 0, reprompt INTEGER DEFAULT 0,
            created_at TEXT NOT NULL, updated_at TEXT NOT NULL, deleted_at TEXT,
            FOREIGN KEY (user_id) REFERENCES accounts(id)
        );
        CREATE TABLE IF NOT EXISTS folders (
            id TEXT PRIMARY KEY, user_id TEXT NOT NULL, name TEXT NOT NULL,
            created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
            FOREIGN KEY (user_id) REFERENCES accounts(id)
        );
        CREATE TABLE IF NOT EXISTS tokens (
            refresh_token TEXT PRIMARY KEY, user_id TEXT NOT NULL, created_at TEXT NOT NULL,
            FOREIGN KEY (user_id) REFERENCES accounts(id)
        );
    )");

    auto rows = db.query("SELECT value FROM config WHERE key='jwt_secret'");
    if (!rows.empty()) {
        g_jwt_secret = rows[0]["value"].get<std::string>();
    } else {
        g_jwt_secret = generate_hex(32);
        db.run("INSERT INTO config (key, value) VALUES ('jwt_secret', ?)", {g_jwt_secret});
    }
}

// ============================================================================
// JSON Builders
// ============================================================================
json build_profile(const json& u) {
    std::string name = u.contains("name") && u["name"].is_string() ? u["name"].get<std::string>() : "";
    std::string email = u["email"].get<std::string>();
    if (name.empty()) name = email.substr(0, email.find('@'));

    auto safe_str = [&](const char* key) -> json {
        if (!u.contains(key) || u[key].is_null()) return "";
        return u[key];
    };

    return {
        {"object", "profile"}, {"id", u["id"]}, {"name", name}, {"email", email},
        {"emailVerified", true}, {"premium", true}, {"premiumFromOrganization", false},
        {"masterPasswordHint", safe_str("master_password_hint")},
        {"culture", safe_str("culture")},
        {"twoFactorEnabled", false}, {"key", safe_str("key")},
        {"privateKey", safe_str("encrypted_private_key")},
        {"securityStamp", safe_str("security_stamp")},
        {"forcePasswordReset", false}, {"usesKeyConnector", false}, {"avatarColor", nullptr},
        {"organizations", json::array()}, {"providers", json::array()},
        {"providerOrganizations", json::array()},
    };
}

json extract_cipher_data(const json& body) {
    json d;
    for (auto& f : {"name","notes","login","card","identity","secureNote",
                     "fields","passwordHistory","attachments","reprompt","key"})
        if (body.contains(f)) d[f] = body[f];
    return d;
}

json build_cipher(const json& r) {
    json data;
    if (r.contains("data") && r["data"].is_string()) {
        try { data = json::parse(r["data"].get<std::string>()); } catch (...) {}
    }

    auto dv = [&](const char* k) -> json {
        return data.contains(k) ? data[k] : json(nullptr);
    };

    return {
        {"object", "cipher"}, {"id", r["id"]},
        {"organizationId", r.contains("organization_id") ? r["organization_id"] : json(nullptr)},
        {"folderId", r.contains("folder_id") ? r["folder_id"] : json(nullptr)},
        {"type", r.value("type", (int64_t)1)},
        {"name", dv("name")}, {"notes", dv("notes")}, {"login", dv("login")},
        {"card", dv("card")}, {"identity", dv("identity")}, {"secureNote", dv("secureNote")},
        {"fields", dv("fields")}, {"passwordHistory", dv("passwordHistory")},
        {"attachments", dv("attachments")}, {"key", dv("key")},
        {"favorite", r.value("favorite", (int64_t)0) != 0},
        {"reprompt", r.value("reprompt", (int64_t)0)},
        {"organizationUseTotp", false},
        {"revisionDate", r.value("updated_at", std::string(""))},
        {"creationDate", r.value("created_at", std::string(""))},
        {"deletedDate", r.contains("deleted_at") ? r["deleted_at"] : json(nullptr)},
        {"collectionIds", json::array()}, {"edit", true}, {"viewPassword", true},
    };
}

json build_folder(const json& r) {
    return {
        {"object", "folder"}, {"id", r["id"]}, {"name", r["name"]},
        {"revisionDate", r.value("updated_at", std::string(""))},
    };
}

// ============================================================================
// Auth Helper
// ============================================================================
json get_current_user(const httplib::Request& req, httplib::Response& res) {
    auto auth = req.get_header_value("Authorization");
    if (auth.size() < 8 || auth.substr(0, 7) != "Bearer ") {
        send_error(res, 401, "Unauthorized");
        return json();
    }
    try {
        auto payload = jwt_decode(auth.substr(7));
        DB db;
        auto rows = db.query("SELECT * FROM accounts WHERE id=?", {payload["sub"].get<std::string>()});
        if (rows.empty()) { send_error(res, 401, "User not found"); return json(); }
        return rows[0];
    } catch (const std::exception& e) {
        send_error(res, 401, e.what());
        return json();
    }
}

// ============================================================================
// Cipher Upsert
// ============================================================================
json upsert_cipher(const std::string& user_id, const std::string& cipher_id, const json& body) {
    std::string now = utcnow();
    std::string ct = std::to_string(body.value("type", 1));
    std::string fid = body.contains("folderId") && body["folderId"].is_string() ? body["folderId"].get<std::string>() : "";
    std::string oid = body.contains("organizationId") && body["organizationId"].is_string() ? body["organizationId"].get<std::string>() : "";
    std::string fav = body.value("favorite", false) ? "1" : "0";
    std::string rep = std::to_string(body.value("reprompt", 0));
    std::string data = extract_cipher_data(body).dump();

    DB db;
    std::string cid = cipher_id;
    if (!cid.empty()) {
        db.run("UPDATE ciphers SET folder_id=NULLIF(?,''), organization_id=NULLIF(?,''), type=?, data=?, favorite=?, reprompt=?, updated_at=? WHERE id=? AND user_id=?",
            {fid, oid, ct, data, fav, rep, now, cid, user_id});
    } else {
        cid = generate_uuid();
        db.run("INSERT INTO ciphers (id, user_id, folder_id, organization_id, type, data, favorite, reprompt, created_at, updated_at) VALUES (?, ?, NULLIF(?,''), NULLIF(?,''), ?, ?, ?, ?, ?, ?)",
            {cid, user_id, fid, oid, ct, data, fav, rep, now, now});
    }

    auto rows = db.query("SELECT * FROM ciphers WHERE id=?", {cid});
    return rows.empty() ? json() : build_cipher(rows[0]);
}

// ============================================================================
// Refresh Token
// ============================================================================
std::string create_refresh_token(const std::string& uid) {
    std::string tok = generate_uuid();
    DB db;
    db.run("INSERT INTO tokens (refresh_token, user_id, created_at) VALUES (?, ?, ?)", {tok, uid, utcnow()});
    return tok;
}

// ============================================================================
// Route Setup
// ============================================================================
void setup_routes(httplib::Server& svr) {

    // --- CORS ---
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "*");
        res.set_header("Access-Control-Allow-Credentials", "true");
    });

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    // --- Health / Info ---
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"server","VaultBox"}, {"version",SERVER_VERSION}, {"status","running"}});
    });

    svr.Get("/alive", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    // --- Prelogin ---
    svr.Post("/accounts/prelogin", [](const httplib::Request& req, httplib::Response& res) {
        auto body = parse_body(req);
        std::string email = to_lower(trim(body.value("email", "")));
        DB db;
        auto rows = db.query("SELECT kdf, kdf_iterations, kdf_memory, kdf_parallelism FROM accounts WHERE email=?", {email});
        if (!rows.empty()) {
            send_json(res, {
                {"kdf", rows[0]["kdf"]}, {"kdfIterations", rows[0]["kdf_iterations"]},
                {"kdfMemory", rows[0]["kdf_memory"]}, {"kdfParallelism", rows[0]["kdf_parallelism"]},
            });
        } else {
            send_json(res, {{"kdf",1}, {"kdfIterations",3}, {"kdfMemory",64}, {"kdfParallelism",4}});
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
            send_error(res, 400, "Email already registered"); return;
        }
        db.run(R"(INSERT INTO accounts (id, email, master_password_hash, master_password_hint,
            security_stamp, key, public_key, encrypted_private_key,
            kdf, kdf_iterations, kdf_memory, kdf_parallelism, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
            {uid, email, hash, body.value("masterPasswordHint", ""),
             stamp, body.value("userSymmetricKey", ""),
             keys.value("publicKey", ""), keys.value("encryptedPrivateKey", ""),
             std::to_string(body.value("kdf", 1)), std::to_string(body.value("kdfIterations", 600000)),
             body.contains("kdfMemory") && !body["kdfMemory"].is_null() ? std::to_string(body["kdfMemory"].get<int>()) : "",
             body.contains("kdfParallelism") && !body["kdfParallelism"].is_null() ? std::to_string(body["kdfParallelism"].get<int>()) : "",
             now, now});
        res.status = 200;
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
            send_error(res, 400, "Email already registered"); return;
        }
        db.run(R"(INSERT INTO accounts (id, email, master_password_hash, master_password_hint,
            security_stamp, key, public_key, encrypted_private_key,
            kdf, kdf_iterations, kdf_memory, kdf_parallelism, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULLIF(?,''), NULLIF(?,''), ?, ?))",
            {uid, email, hash, body.value("masterPasswordHint", ""),
             stamp, body.value("key", ""),
             keys.value("publicKey", ""), keys.value("encryptedPrivateKey", ""),
             std::to_string(body.value("kdf", 1)), std::to_string(body.value("kdfIterations", 600000)),
             body.contains("kdfMemory") && !body["kdfMemory"].is_null() ? std::to_string(body["kdfMemory"].get<int>()) : "",
             body.contains("kdfParallelism") && !body["kdfParallelism"].is_null() ? std::to_string(body["kdfParallelism"].get<int>()) : "",
             now, now});
        res.status = 200;
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

        send_json(res, {
            {"access_token", create_access_token(user["id"].get<std::string>(), user["email"].get<std::string>())},
            {"expires_in", TOKEN_EXPIRY_HOURS * 3600},
            {"token_type", "Bearer"},
            {"refresh_token", create_refresh_token(user["id"].get<std::string>())},
            {"Key", safe("key")}, {"PrivateKey", safe("encrypted_private_key")},
            {"Kdf", safe("kdf")}, {"KdfIterations", safe("kdf_iterations")},
            {"KdfMemory", safe("kdf_memory")}, {"KdfParallelism", safe("kdf_parallelism")},
            {"ResetMasterPassword", false}, {"ForcePasswordReset", false},
            {"MasterPasswordPolicy", nullptr},
            {"UserDecryptionOptions", {{"HasMasterPassword", true}}},
            {"scope", "api offline_access"},
        });
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
            {std::to_string(body.value("kdf", (int)user.value("kdf",(int64_t)1))),
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

    // --- Ciphers (specific routes first) ---
    svr.Post("/ciphers/create", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        auto cipher = body.contains("cipher") ? body["cipher"] : body;
        auto r = upsert_cipher(user["id"].get<std::string>(), "", cipher);
        send_json(res, r);
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
    });

    // Cipher detail routes
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

    // Soft delete
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

    // Restore
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

    // Partial update (move to folder, toggle favorite)
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

    // Toggle favorite
    svr.Put(R"(/ciphers/([^/]+)/favorite)", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        auto body = parse_body(req);
        DB db;
        db.run("UPDATE ciphers SET favorite=?, updated_at=? WHERE id=? AND user_id=?",
            {body.value("favorite",false) ? "1" : "0", utcnow(), req.matches[1].str(), user["id"].get<std::string>()});
        res.status = 200;
    });

    // Collections stubs
    auto coll_stub = [](const httplib::Request&, httplib::Response& res) { res.status = 200; };
    svr.Post(R"(/ciphers/([^/]+)/collections)", coll_stub);
    svr.Post(R"(/ciphers/([^/]+)/collections-admin)", coll_stub);

    // Create cipher (POST /ciphers)
    svr.Post("/ciphers", [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        send_json(res, upsert_cipher(user["id"].get<std::string>(), "", parse_body(req)));
    });

    // Get cipher
    svr.Get(R"(/ciphers/([^/]+))", get_cipher_handler);

    // Update cipher (PUT and POST)
    auto update_cipher = [](const httplib::Request& req, httplib::Response& res) {
        auto user = get_current_user(req, res);
        if (user.is_null()) return;
        send_json(res, upsert_cipher(user["id"].get<std::string>(), req.matches[1].str(), parse_body(req)));
    };
    svr.Put(R"(/ciphers/([^/]+))", update_cipher);

    // Hard delete cipher
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

    // Delete account
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
    };
    svr.Post("/accounts/delete", delete_account);
    svr.Delete("/accounts", delete_account);

    svr.Post("/accounts/verify-devices", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

    // Events (no-op)
    svr.Post("/collect", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
    svr.Post("/events/collect", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

    svr.Get(R"(/auth-requests/([^/]+))", [](const httplib::Request&, httplib::Response& res) {
        send_error(res, 404, "Not found");
    });

    // --- Exception handler ---
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { send_error(res, 500, e.what()); }
        catch (...) { send_error(res, 500, "Internal server error"); }
    });

    // --- Catch-all for unimplemented endpoints ---
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404) {
            res.status = 200;
            if (req.method == "GET") {
                res.set_content(R"({"object":"list","data":[],"continuationToken":null})", "application/json");
            }
        }
    });
}

// ============================================================================
// System Tray
// ============================================================================
static NOTIFYICONDATAW g_nid = {};
static HWND g_tray_hwnd = nullptr;

HICON create_tray_icon() {
    int sz = 32;
    HDC hdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, sz, sz);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    SelectObject(mem, bmp);

    // Dark background
    HBRUSH bg = CreateSolidBrush(RGB(13, 14, 26));
    RECT rc = {0, 0, sz, sz};
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    // Blue vault body
    HBRUSH blue = CreateSolidBrush(RGB(59, 130, 246));
    RECT vault = {7, 14, 25, 27};
    FillRect(mem, &vault, blue);
    DeleteObject(blue);

    // Green status dot
    HBRUSH green = CreateSolidBrush(RGB(34, 197, 94));
    HBRUSH oldBrush = (HBRUSH)SelectObject(mem, green);
    Ellipse(mem, 22, 2, 30, 10);
    SelectObject(mem, oldBrush);
    DeleteObject(green);

    // Mask (all opaque)
    HDC mdc = CreateCompatibleDC(hdc);
    SelectObject(mdc, mask);
    RECT mrc = {0, 0, sz, sz};
    FillRect(mdc, &mrc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    DeleteDC(mdc);

    DeleteDC(mem);
    ReleaseDC(nullptr, hdc);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = bmp;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    DeleteObject(mask);
    return icon;
}

LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAYICON) {
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"VaultBox Server v0.3.0");
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"http://127.0.0.1:8787");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Open Data Folder");
            AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"Quit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wp) == ID_TRAY_OPEN) {
            ShellExecuteW(nullptr, L"open", g_data_dir.wstring().c_str(), nullptr, nullptr, SW_SHOW);
        } else if (LOWORD(wp) == ID_TRAY_QUIT) {
            g_shutdown = true;
            if (g_server) g_server->stop();
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void start_tray() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = tray_wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VaultBoxTray";
    RegisterClassExW(&wc);

    g_tray_hwnd = CreateWindowExW(0, L"VaultBoxTray", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_tray_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = create_tray_icon();
    wcscpy_s(g_nid.szTip, L"VaultBox Server");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ============================================================================
// Main
// ============================================================================
int main() {
    // Single-instance mutex
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Global\\VaultBoxServerMutex");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    // Init data directory
    const char* custom = getenv("VAULTBOX_DATA");
    if (custom) {
        g_data_dir = fs::path(custom);
    } else {
        const char* la = getenv("LOCALAPPDATA");
        g_data_dir = fs::path(la ? la : ".") / "VaultBox";
    }
    g_db_path = g_data_dir / "vault.db";

    init_db();

    // Start tray in background thread
    std::thread tray(start_tray);
    tray.detach();

    // Setup and start HTTP server
    httplib::Server svr;
    g_server = &svr;
    setup_routes(svr);

    svr.listen(HOST, PORT);

    // Cleanup
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    CloseHandle(mutex);
    return 0;
}
