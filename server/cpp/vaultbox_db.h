// VaultBox Desktop - Database Layer
#pragma once
#include "vaultbox_server.h"

// ============================================================================
// SQLite Wrapper
// ============================================================================
class DB {
    sqlite3* db_ = nullptr;
public:
    DB() {
        int rc = sqlite3_open(g_db_path.string().c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::string err = db_ ? sqlite3_errmsg(db_) : "open failed";
            if (db_) { sqlite3_close(db_); db_ = nullptr; }
            throw std::runtime_error("sqlite3_open failed: " + err);
        }
        sqlite3_busy_timeout(db_, 5000);
        // WAL gives better concurrent read performance; synchronous=NORMAL is safe
        // with WAL (durable on full OS crash is not guaranteed but DB stays consistent).
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;"
                          "PRAGMA foreign_keys=ON;"
                          "PRAGMA synchronous=NORMAL;"
                          "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
    }
    ~DB() { if (db_) sqlite3_close(db_); }
    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;

    bool ok() const { return db_ != nullptr; }

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

// ============================================================================
// Schema Init
// ============================================================================
inline void init_db() {
    std::error_code ec;
    fs::create_directories(g_data_dir, ec);
    if (ec) throw std::runtime_error("cannot create data directory: " + ec.message());

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
        CREATE INDEX IF NOT EXISTS idx_ciphers_user      ON ciphers(user_id);
        CREATE INDEX IF NOT EXISTS idx_ciphers_user_del  ON ciphers(user_id, deleted_at);
        CREATE INDEX IF NOT EXISTS idx_ciphers_folder    ON ciphers(folder_id);
        CREATE INDEX IF NOT EXISTS idx_folders_user      ON folders(user_id);
        CREATE INDEX IF NOT EXISTS idx_tokens_user       ON tokens(user_id);
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
inline json build_account_keys(const json& u) {
    auto safe_str = [&](const char* key) -> std::string {
        if (!u.contains(key) || u[key].is_null()) return "";
        return u[key].is_string() ? u[key].get<std::string>() : "";
    };
    std::string pub = safe_str("public_key");
    std::string priv = safe_str("encrypted_private_key");
    if (!pub.empty() && !priv.empty()) {
        return {{"publicKeyEncryptionKeyPair", {{"publicKey", pub}, {"wrappedPrivateKey", priv}}}};
    }
    return nullptr;
}

inline json build_profile(const json& u) {
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
        {"AccountKeys", build_account_keys(u)},
        {"securityStamp", safe_str("security_stamp")},
        {"forcePasswordReset", false}, {"usesKeyConnector", false}, {"avatarColor", nullptr},
        // Extension's ProfileResponse reads CreationDate and VerifyDevices.
        // Both fall back to sane defaults if absent, but emitting them keeps
        // the response in line with what the upstream Bitwarden server sends
        // and avoids spurious null fields in downstream observers.
        {"creationDate", safe_str("created_at")},
        {"verifyDevices", true},
        {"organizations", json::array()}, {"providers", json::array()},
        {"providerOrganizations", json::array()},
    };
}

inline json extract_cipher_data(const json& body) {
    json d;
    for (auto& f : {"name","notes","login","card","identity","secureNote",
                     "fields","passwordHistory","attachments","reprompt","key"})
        if (body.contains(f)) d[f] = body[f];
    return d;
}

inline json build_cipher(const json& r) {
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
        {"type", r.contains("type") ? to_int(r["type"], 1) : 1},
        {"name", dv("name")}, {"notes", dv("notes")}, {"login", dv("login")},
        {"card", dv("card")}, {"identity", dv("identity")}, {"secureNote", dv("secureNote")},
        {"fields", dv("fields")}, {"passwordHistory", dv("passwordHistory")},
        {"attachments", dv("attachments")}, {"key", dv("key")},
        {"favorite", r.contains("favorite") ? to_int(r["favorite"], 0) != 0 : false},
        {"reprompt", r.contains("reprompt") ? to_int(r["reprompt"], 0) : 0},
        {"organizationUseTotp", false},
        {"revisionDate", r.value("updated_at", std::string(""))},
        {"creationDate", r.value("created_at", std::string(""))},
        {"deletedDate", r.contains("deleted_at") ? r["deleted_at"] : json(nullptr)},
        {"collectionIds", json::array()}, {"edit", true}, {"viewPassword", true},
    };
}

inline json build_folder(const json& r) {
    return {
        {"object", "folder"}, {"id", r["id"]}, {"name", r["name"]},
        {"revisionDate", r.value("updated_at", std::string(""))},
    };
}

// ============================================================================
// Auth & Cipher Helpers
// ============================================================================
inline std::vector<uint8_t> hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len) {
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

    BCryptCreateHash(hAlg, &hHash, obj.data(), objLen, (PBYTE)key, (ULONG)key_len, 0);
    BCryptHashData(hHash, (PBYTE)data, (ULONG)data_len, 0);
    BCryptFinishHash(hHash, result.data(), hashLen, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}

inline std::vector<uint8_t> hmac_sha256(const std::string& key, const std::string& data) {
    return hmac_sha256((const uint8_t*)key.data(), key.size(), (const uint8_t*)data.data(), data.size());
}

// JWT
inline std::string jwt_encode(const json& payload) {
    std::string h = base64url_encode(json({{"alg","HS256"},{"typ","JWT"}}).dump());
    std::string p = base64url_encode(payload.dump());
    std::string msg = h + "." + p;
    auto sig = hmac_sha256(g_jwt_secret, msg);
    return msg + "." + base64url_encode(sig.data(), sig.size());
}

inline json jwt_decode(const std::string& token) {
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

inline std::string create_access_token(const std::string& uid, const std::string& email) {
    int64_t now = current_timestamp();
    return jwt_encode({
        {"sub", uid}, {"email", email}, {"name", email.substr(0, email.find('@'))},
        {"premium", true}, {"email_verified", true}, {"iss", "vaultbox|local"},
        {"iat", now}, {"nbf", now}, {"exp", now + TOKEN_EXPIRY_HOURS * 3600},
        {"scope", json::array({"api", "offline_access"})},
        {"amr", json::array({"Application"})},
    });
}

inline json get_current_user(const httplib::Request& req, httplib::Response& res) {
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

inline json upsert_cipher(const std::string& user_id, const std::string& cipher_id, const json& body) {
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

inline std::string create_refresh_token(const std::string& uid) {
    std::string tok = generate_uuid();
    DB db;
    db.run("INSERT INTO tokens (refresh_token, user_id, created_at) VALUES (?, ?, ?)", {tok, uid, utcnow()});
    return tok;
}
