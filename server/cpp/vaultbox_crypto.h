// VaultBox Desktop - Bitwarden Crypto Engine (Windows BCrypt API)
// Full decryption chain: Master Password -> PBKDF2 -> HKDF-Expand -> AES-256-CBC
#pragma once
#include "vaultbox_server.h"
#include "vaultbox_db.h"

namespace VBCrypto {

// ============================================================================
// PBKDF2-SHA256
// ============================================================================
inline std::vector<uint8_t> pbkdf2_sha256(const std::string& password, const std::vector<uint8_t>& salt, int iterations, int keyLen = 32) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))
        return {};

    std::vector<uint8_t> derived(keyLen);
    NTSTATUS status = BCryptDeriveKeyPBKDF2(hAlg,
        (PBYTE)password.data(), (ULONG)password.size(),
        (PBYTE)salt.data(), (ULONG)salt.size(),
        (ULONGLONG)iterations,
        derived.data(), (ULONG)derived.size(), 0);

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return (status == 0) ? derived : std::vector<uint8_t>{};
}

// ============================================================================
// HKDF-Expand (single block, 32 bytes output)
// T(1) = HMAC-SHA256(PRK, info || 0x01)
// ============================================================================
inline std::vector<uint8_t> hkdf_expand(const std::vector<uint8_t>& prk, const std::string& info, int outLen = 32) {
    // For 32 bytes we only need one HMAC block
    std::vector<uint8_t> input(info.begin(), info.end());
    input.push_back(0x01);

    return hmac_sha256(prk.data(), prk.size(), input.data(), input.size());
}

// ============================================================================
// AES-256-CBC Decrypt (no padding removal needed - BCrypt handles it)
// ============================================================================
inline std::vector<uint8_t> aes256_cbc_decrypt(const uint8_t* key, size_t keyLen,
                                                 const uint8_t* iv, size_t ivLen,
                                                 const uint8_t* ct, size_t ctLen) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    std::vector<uint8_t> result;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))
        return result;

    // Set CBC mode
    NTSTATUS s = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (s != 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return result; }

    // Get key object size
    DWORD objLen = 0, tmp = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&objLen, sizeof(objLen), &tmp, 0);
    std::vector<uint8_t> keyObj(objLen);

    s = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), objLen,
        (PBYTE)key, (ULONG)keyLen, 0);
    if (s != 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return result; }

    // Copy IV (BCrypt modifies it in place)
    std::vector<uint8_t> ivCopy(iv, iv + ivLen);

    // Determine output size
    DWORD plainLen = 0;
    s = BCryptDecrypt(hKey, (PBYTE)ct, (ULONG)ctLen, nullptr,
        ivCopy.data(), (ULONG)ivCopy.size(), nullptr, 0, &plainLen, BCRYPT_BLOCK_PADDING);
    if (s != 0) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return result; }

    result.resize(plainLen);
    // Reset IV copy
    ivCopy.assign(iv, iv + ivLen);

    s = BCryptDecrypt(hKey, (PBYTE)ct, (ULONG)ctLen, nullptr,
        ivCopy.data(), (ULONG)ivCopy.size(), result.data(), (ULONG)result.size(),
        &plainLen, BCRYPT_BLOCK_PADDING);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (s != 0) return {};
    result.resize(plainLen);
    return result;
}

// ============================================================================
// AES-256-CBC Encrypt
// ============================================================================
inline std::vector<uint8_t> aes256_cbc_encrypt(const uint8_t* key, size_t keyLen,
                                                 const uint8_t* iv, size_t ivLen,
                                                 const uint8_t* pt, size_t ptLen) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    std::vector<uint8_t> result;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))
        return result;

    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);

    DWORD objLen = 0, tmp = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&objLen, sizeof(objLen), &tmp, 0);
    std::vector<uint8_t> keyObj(objLen);

    BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), objLen,
        (PBYTE)key, (ULONG)keyLen, 0);

    std::vector<uint8_t> ivCopy(iv, iv + ivLen);

    DWORD ctLen = 0;
    BCryptEncrypt(hKey, (PBYTE)pt, (ULONG)ptLen, nullptr,
        ivCopy.data(), (ULONG)ivCopy.size(), nullptr, 0, &ctLen, BCRYPT_BLOCK_PADDING);

    result.resize(ctLen);
    ivCopy.assign(iv, iv + ivLen);

    BCryptEncrypt(hKey, (PBYTE)pt, (ULONG)ptLen, nullptr,
        ivCopy.data(), (ULONG)ivCopy.size(), result.data(), (ULONG)result.size(),
        &ctLen, BCRYPT_BLOCK_PADDING);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    result.resize(ctLen);
    return result;
}

// ============================================================================
// Generate random bytes (CSPRNG)
// ============================================================================
inline std::vector<uint8_t> random_bytes(size_t count) {
    std::vector<uint8_t> buf(count);
    BCryptGenRandom(nullptr, buf.data(), (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return buf;
}

// ============================================================================
// Parse EncString: "2.IV_b64|CT_b64|MAC_b64"
// ============================================================================
struct EncString {
    int type = -1;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> ct;
    std::vector<uint8_t> mac;

    bool valid() const { return type == 2 && !iv.empty() && !ct.empty() && !mac.empty(); }

    static EncString parse(const std::string& s) {
        EncString es;
        if (s.empty()) return es;

        // Format: "2.IV_b64|CT_b64|MAC_b64"
        auto dot = s.find('.');
        if (dot == std::string::npos) return es;

        try { es.type = std::stoi(s.substr(0, dot)); } catch (...) { return es; }
        if (es.type != 2) return es; // Only support AES-256-CBC + HMAC-SHA256

        std::string rest = s.substr(dot + 1);
        auto p1 = rest.find('|');
        if (p1 == std::string::npos) return es;
        auto p2 = rest.find('|', p1 + 1);
        if (p2 == std::string::npos) return es;

        es.iv = base64_decode(rest.substr(0, p1));
        es.ct = base64_decode(rest.substr(p1 + 1, p2 - p1 - 1));
        es.mac = base64_decode(rest.substr(p2 + 1));
        return es;
    }
};

// ============================================================================
// Decrypt an EncString with a symmetric key pair
// ============================================================================
inline std::string decrypt_encstring(const std::string& encStr, const uint8_t* encKey, const uint8_t* macKey) {
    if (encStr.empty()) return "";

    auto es = EncString::parse(encStr);
    if (!es.valid()) return "";

    // Verify HMAC: HMAC-SHA256(macKey, IV || CT)
    std::vector<uint8_t> macData;
    macData.insert(macData.end(), es.iv.begin(), es.iv.end());
    macData.insert(macData.end(), es.ct.begin(), es.ct.end());

    auto computed_mac = hmac_sha256(macKey, 32, macData.data(), macData.size());
    if (computed_mac.size() != es.mac.size()) return "";

    volatile uint8_t diff = 0;
    for (size_t i = 0; i < computed_mac.size(); i++) diff |= computed_mac[i] ^ es.mac[i];
    if (diff != 0) return "";

    // Decrypt: AES-256-CBC
    auto plain = aes256_cbc_decrypt(encKey, 32, es.iv.data(), es.iv.size(), es.ct.data(), es.ct.size());
    if (plain.empty()) return "";

    return std::string(plain.begin(), plain.end());
}

// ============================================================================
// Encrypt a plaintext string to EncString format "2.IV|CT|MAC"
// ============================================================================
inline std::string encrypt_to_encstring(const std::string& plaintext, const uint8_t* encKey, const uint8_t* macKey) {
    if (plaintext.empty()) return "";

    // Generate random 16-byte IV
    auto iv = random_bytes(16);

    // Encrypt
    auto ct = aes256_cbc_encrypt(encKey, 32, iv.data(), iv.size(),
        (const uint8_t*)plaintext.data(), plaintext.size());
    if (ct.empty()) return "";

    // HMAC: HMAC-SHA256(macKey, IV || CT)
    std::vector<uint8_t> macData;
    macData.insert(macData.end(), iv.begin(), iv.end());
    macData.insert(macData.end(), ct.begin(), ct.end());
    auto mac = hmac_sha256(macKey, 32, macData.data(), macData.size());

    // Format: "2.IV_b64|CT_b64|MAC_b64"
    return "2." + base64_encode(iv.data(), iv.size()) + "|" +
           base64_encode(ct.data(), ct.size()) + "|" +
           base64_encode(mac.data(), mac.size());
}

// ============================================================================
// Derive Stretched Key from Master Password
// masterPassword + email -> PBKDF2(600K) -> masterKey(32)
// -> HKDF-Expand("enc", 32) + HKDF-Expand("mac", 32) -> stretchedKey(64)
// ============================================================================
struct SymmetricKey {
    std::vector<uint8_t> encKey; // 32 bytes
    std::vector<uint8_t> macKey; // 32 bytes
    bool valid() const { return encKey.size() == 32 && macKey.size() == 32; }
    void clear() {
        if (!encKey.empty()) SecureZeroMemory(encKey.data(), encKey.size());
        if (!macKey.empty()) SecureZeroMemory(macKey.data(), macKey.size());
        encKey.clear();
        macKey.clear();
    }
};

inline SymmetricKey derive_stretched_key(const std::string& password, const std::string& email, int iterations = 600000) {
    SymmetricKey sk;
    // Salt = lowercase email as UTF-8 bytes
    std::string emailLower = to_lower(email);
    std::vector<uint8_t> salt(emailLower.begin(), emailLower.end());

    // PBKDF2-SHA256
    auto masterKey = pbkdf2_sha256(password, salt, iterations, 32);
    if (masterKey.size() != 32) return sk;

    // HKDF-Expand for enc and mac
    sk.encKey = hkdf_expand(masterKey, "enc", 32);
    sk.macKey = hkdf_expand(masterKey, "mac", 32);

    // Clear master key from memory
    SecureZeroMemory(masterKey.data(), masterKey.size());
    return sk;
}

// ============================================================================
// Decrypt the user's symmetric key using the stretched key
// accounts.key = EncString containing the 64-byte user key
// ============================================================================
inline SymmetricKey decrypt_user_key(const SymmetricKey& stretched, const std::string& encKeyStr) {
    SymmetricKey userKey;
    if (!stretched.valid() || encKeyStr.empty()) return userKey;

    std::string plainKey = decrypt_encstring(encKeyStr, stretched.encKey.data(), stretched.macKey.data());
    if (plainKey.size() != 64 && plainKey.size() != 32) return userKey;

    if (plainKey.size() == 64) {
        userKey.encKey.assign(plainKey.begin(), plainKey.begin() + 32);
        userKey.macKey.assign(plainKey.begin() + 32, plainKey.end());
    } else {
        // 32-byte key: encKey only, no MAC key (legacy)
        userKey.encKey.assign(plainKey.begin(), plainKey.end());
        userKey.macKey.resize(32, 0);
    }

    SecureZeroMemory(&plainKey[0], plainKey.size());
    return userKey;
}

// ============================================================================
// Unlock vault: derive keys, decrypt user key, decrypt all entries
// ============================================================================
inline bool unlock_vault(const std::string& password, const std::string& email) {
    g_vault.clear();

    DB db;
    auto users = db.query("SELECT * FROM accounts WHERE email=?", {to_lower(email)});
    if (users.empty()) {
        vb_log("Unlock failed: no account found for " + email);
        return false;
    }

    auto& user = users[0];
    std::string userId = user["id"].get<std::string>();
    int iterations = to_int(user["kdf_iterations"], 600000);
    std::string encKeyStr = user.contains("key") && user["key"].is_string() ? user["key"].get<std::string>() : "";

    if (encKeyStr.empty()) {
        vb_log("Unlock failed: no encrypted key stored for account");
        return false;
    }

    vb_log("Deriving keys (PBKDF2, " + std::to_string(iterations) + " iterations)...");

    // Derive stretched key from password
    auto stretched = derive_stretched_key(password, email, iterations);
    if (!stretched.valid()) {
        vb_log("Unlock failed: key derivation error");
        return false;
    }

    // Decrypt user key
    auto userKey = decrypt_user_key(stretched, encKeyStr);
    stretched.clear();

    if (!userKey.valid()) {
        vb_log("Unlock failed: wrong master password or corrupted key");
        return false;
    }

    // Store user key in vault state
    {
        std::lock_guard<std::mutex> lk(g_vault.mtx);
        g_vault.userEncKey = userKey.encKey;
        g_vault.userMacKey = userKey.macKey;
        g_vault.userId = userId;
        g_vault.email = to_lower(email);
        g_vault.unlocked = true;
    }

    // Decrypt all folders
    auto folderRows = db.query("SELECT * FROM folders WHERE user_id=? ORDER BY name", {userId});
    for (auto& fr : folderRows) {
        DecryptedFolder df;
        df.id = fr["id"].get<std::string>();
        std::string encName = fr["name"].is_string() ? fr["name"].get<std::string>() : "";
        df.name = decrypt_encstring(encName, userKey.encKey.data(), userKey.macKey.data());
        if (df.name.empty()) df.name = "(unnamed)";
        g_vault.folders.push_back(df);
    }

    // Decrypt all ciphers
    auto cipherRows = db.query("SELECT * FROM ciphers WHERE user_id=? AND deleted_at IS NULL ORDER BY created_at", {userId});
    for (auto& cr : cipherRows) {
        DecryptedEntry de;
        de.id = cr["id"].get<std::string>();
        de.type = to_int(cr["type"], 1);
        de.favorite = to_int(cr["favorite"], 0) != 0;
        de.folderId = cr.contains("folder_id") && cr["folder_id"].is_string() ? cr["folder_id"].get<std::string>() : "";
        de.updatedAt = cr.value("updated_at", std::string(""));

        // Find folder name
        for (auto& f : g_vault.folders) {
            if (f.id == de.folderId) { de.folderName = f.name; break; }
        }

        // Parse cipher data JSON
        json data;
        if (cr.contains("data") && cr["data"].is_string()) {
            try { data = json::parse(cr["data"].get<std::string>()); } catch (...) {}
        }

        // Decrypt name
        if (data.contains("name") && data["name"].is_string())
            de.name = decrypt_encstring(data["name"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());

        // Decrypt notes
        if (data.contains("notes") && data["notes"].is_string())
            de.notes = decrypt_encstring(data["notes"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());

        // Decrypt login fields
        if (data.contains("login") && data["login"].is_object()) {
            auto& login = data["login"];
            if (login.contains("username") && login["username"].is_string())
                de.username = decrypt_encstring(login["username"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
            if (login.contains("password") && login["password"].is_string())
                de.password = decrypt_encstring(login["password"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
            if (login.contains("uris") && login["uris"].is_array() && !login["uris"].empty()) {
                auto& firstUri = login["uris"][0];
                if (firstUri.contains("uri") && firstUri["uri"].is_string())
                    de.uri = decrypt_encstring(firstUri["uri"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
            }
            if (login.contains("totp") && login["totp"].is_string())
                de.totp = decrypt_encstring(login["totp"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
        }

        // Decrypt card fields
        if (data.contains("card") && data["card"].is_object()) {
            auto& card = data["card"];
            if (card.contains("cardholderName") && card["cardholderName"].is_string())
                de.username = decrypt_encstring(card["cardholderName"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
            if (card.contains("number") && card["number"].is_string())
                de.password = decrypt_encstring(card["number"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
        }

        // Decrypt identity fields
        if (data.contains("identity") && data["identity"].is_object()) {
            auto& ident = data["identity"];
            std::string firstName, lastName;
            if (ident.contains("firstName") && ident["firstName"].is_string())
                firstName = decrypt_encstring(ident["firstName"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
            if (ident.contains("lastName") && ident["lastName"].is_string())
                lastName = decrypt_encstring(ident["lastName"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
            de.username = firstName + (firstName.empty() || lastName.empty() ? "" : " ") + lastName;
            if (ident.contains("email") && ident["email"].is_string())
                de.uri = decrypt_encstring(ident["email"].get<std::string>(), userKey.encKey.data(), userKey.macKey.data());
        }

        g_vault.entries.push_back(std::move(de));
    }

    vb_log("Vault unlocked: " + std::to_string(g_vault.entries.size()) + " entries, " +
           std::to_string(g_vault.folders.size()) + " folders");
    return true;
}

// ============================================================================
// Re-encrypt and save a new/updated entry to the database
// ============================================================================
inline bool save_entry(DecryptedEntry& de, bool isNew) {
    if (!g_vault.unlocked) return false;

    const uint8_t* ek = g_vault.userEncKey.data();
    const uint8_t* mk = g_vault.userMacKey.data();

    // Build cipher data JSON with encrypted fields
    json data;
    data["name"] = encrypt_to_encstring(de.name, ek, mk);
    if (!de.notes.empty()) data["notes"] = encrypt_to_encstring(de.notes, ek, mk);

    if (de.type == 1) { // Login
        json login;
        if (!de.username.empty()) login["username"] = encrypt_to_encstring(de.username, ek, mk);
        if (!de.password.empty()) login["password"] = encrypt_to_encstring(de.password, ek, mk);
        if (!de.uri.empty()) {
            login["uris"] = json::array({json({{"uri", encrypt_to_encstring(de.uri, ek, mk)}, {"match", nullptr}})});
        }
        if (!de.totp.empty()) login["totp"] = encrypt_to_encstring(de.totp, ek, mk);
        data["login"] = login;
    } else if (de.type == 3) { // Card
        json card;
        if (!de.username.empty()) card["cardholderName"] = encrypt_to_encstring(de.username, ek, mk);
        if (!de.password.empty()) card["number"] = encrypt_to_encstring(de.password, ek, mk);
        data["card"] = card;
    } else if (de.type == 4) { // Identity
        json ident;
        auto sp = de.username.find(' ');
        if (sp != std::string::npos) {
            ident["firstName"] = encrypt_to_encstring(de.username.substr(0, sp), ek, mk);
            ident["lastName"] = encrypt_to_encstring(de.username.substr(sp + 1), ek, mk);
        } else {
            ident["firstName"] = encrypt_to_encstring(de.username, ek, mk);
        }
        if (!de.uri.empty()) ident["email"] = encrypt_to_encstring(de.uri, ek, mk);
        data["identity"] = ident;
    } else if (de.type == 2) { // SecureNote
        data["secureNote"] = json({{"type", 0}});
    }

    std::string now = utcnow();
    std::string dataStr = data.dump();
    std::string typeStr = std::to_string(de.type);
    std::string favStr = de.favorite ? "1" : "0";

    DB db;
    if (isNew) {
        de.id = generate_uuid();
        db.run("INSERT INTO ciphers (id, user_id, folder_id, organization_id, type, data, favorite, reprompt, created_at, updated_at) VALUES (?, ?, NULLIF(?,''), NULL, ?, ?, ?, 0, ?, ?)",
            {de.id, g_vault.userId, de.folderId, typeStr, dataStr, favStr, now, now});
    } else {
        db.run("UPDATE ciphers SET folder_id=NULLIF(?,''), type=?, data=?, favorite=?, updated_at=? WHERE id=? AND user_id=?",
            {de.folderId, typeStr, dataStr, favStr, now, de.id, g_vault.userId});
    }

    de.updatedAt = now;
    return true;
}

// ============================================================================
// Delete entry from database
// ============================================================================
inline bool delete_entry(const std::string& id) {
    if (!g_vault.unlocked) return false;
    DB db;
    db.run("DELETE FROM ciphers WHERE id=? AND user_id=?", {id, g_vault.userId});
    return true;
}

// ============================================================================
// Folder CRUD (encrypted names stored in DB)
// ============================================================================
inline std::string save_folder(const std::string& name, const std::string& existingId = "") {
    if (!g_vault.unlocked || name.empty()) return "";
    std::string encName = encrypt_to_encstring(name, g_vault.userEncKey.data(), g_vault.userMacKey.data());
    std::string now = utcnow();
    DB db;
    if (existingId.empty()) {
        std::string fid = generate_uuid();
        db.run("INSERT INTO folders (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
            {fid, g_vault.userId, encName, now, now});
        return fid;
    } else {
        db.run("UPDATE folders SET name=?, updated_at=? WHERE id=? AND user_id=?",
            {encName, now, existingId, g_vault.userId});
        return existingId;
    }
}

inline bool delete_folder(const std::string& id) {
    if (!g_vault.unlocked) return false;
    DB db;
    db.run("UPDATE ciphers SET folder_id=NULL, updated_at=? WHERE folder_id=? AND user_id=?",
        {utcnow(), id, g_vault.userId});
    db.run("DELETE FROM folders WHERE id=? AND user_id=?", {id, g_vault.userId});
    return true;
}

// ============================================================================
// Refresh decrypted vault data from database
// ============================================================================
inline void refresh_vault() {
    if (!g_vault.unlocked) return;
    std::string password_placeholder; // We don't need to re-derive - we have the key
    std::string email = g_vault.email;
    std::string userId = g_vault.userId;
    auto encKey = g_vault.userEncKey;
    auto macKey = g_vault.userMacKey;

    // Clear entries/folders but keep keys
    g_vault.entries.clear();
    g_vault.folders.clear();

    DB db;
    // Re-decrypt folders
    auto folderRows = db.query("SELECT * FROM folders WHERE user_id=? ORDER BY name", {userId});
    for (auto& fr : folderRows) {
        DecryptedFolder df;
        df.id = fr["id"].get<std::string>();
        std::string encName = fr["name"].is_string() ? fr["name"].get<std::string>() : "";
        df.name = decrypt_encstring(encName, encKey.data(), macKey.data());
        if (df.name.empty()) df.name = "(unnamed)";
        g_vault.folders.push_back(df);
    }

    // Re-decrypt ciphers
    auto cipherRows = db.query("SELECT * FROM ciphers WHERE user_id=? AND deleted_at IS NULL ORDER BY created_at", {userId});
    for (auto& cr : cipherRows) {
        DecryptedEntry de;
        de.id = cr["id"].get<std::string>();
        de.type = to_int(cr["type"], 1);
        de.favorite = to_int(cr["favorite"], 0) != 0;
        de.folderId = cr.contains("folder_id") && cr["folder_id"].is_string() ? cr["folder_id"].get<std::string>() : "";
        de.updatedAt = cr.value("updated_at", std::string(""));

        for (auto& f : g_vault.folders) {
            if (f.id == de.folderId) { de.folderName = f.name; break; }
        }

        json data;
        if (cr.contains("data") && cr["data"].is_string()) {
            try { data = json::parse(cr["data"].get<std::string>()); } catch (...) {}
        }

        if (data.contains("name") && data["name"].is_string())
            de.name = decrypt_encstring(data["name"].get<std::string>(), encKey.data(), macKey.data());
        if (data.contains("notes") && data["notes"].is_string())
            de.notes = decrypt_encstring(data["notes"].get<std::string>(), encKey.data(), macKey.data());
        if (data.contains("login") && data["login"].is_object()) {
            auto& login = data["login"];
            if (login.contains("username") && login["username"].is_string())
                de.username = decrypt_encstring(login["username"].get<std::string>(), encKey.data(), macKey.data());
            if (login.contains("password") && login["password"].is_string())
                de.password = decrypt_encstring(login["password"].get<std::string>(), encKey.data(), macKey.data());
            if (login.contains("uris") && login["uris"].is_array() && !login["uris"].empty()) {
                auto& firstUri = login["uris"][0];
                if (firstUri.contains("uri") && firstUri["uri"].is_string())
                    de.uri = decrypt_encstring(firstUri["uri"].get<std::string>(), encKey.data(), macKey.data());
            }
            if (login.contains("totp") && login["totp"].is_string())
                de.totp = decrypt_encstring(login["totp"].get<std::string>(), encKey.data(), macKey.data());
        }
        if (data.contains("card") && data["card"].is_object()) {
            auto& card = data["card"];
            if (card.contains("cardholderName") && card["cardholderName"].is_string())
                de.username = decrypt_encstring(card["cardholderName"].get<std::string>(), encKey.data(), macKey.data());
            if (card.contains("number") && card["number"].is_string())
                de.password = decrypt_encstring(card["number"].get<std::string>(), encKey.data(), macKey.data());
        }
        if (data.contains("identity") && data["identity"].is_object()) {
            auto& ident = data["identity"];
            std::string firstName, lastName;
            if (ident.contains("firstName") && ident["firstName"].is_string())
                firstName = decrypt_encstring(ident["firstName"].get<std::string>(), encKey.data(), macKey.data());
            if (ident.contains("lastName") && ident["lastName"].is_string())
                lastName = decrypt_encstring(ident["lastName"].get<std::string>(), encKey.data(), macKey.data());
            de.username = firstName + (firstName.empty() || lastName.empty() ? "" : " ") + lastName;
            if (ident.contains("email") && ident["email"].is_string())
                de.uri = decrypt_encstring(ident["email"].get<std::string>(), encKey.data(), macKey.data());
        }

        g_vault.entries.push_back(std::move(de));
    }
}

} // namespace VBCrypto
