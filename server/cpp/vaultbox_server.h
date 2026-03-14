// VaultBox Desktop v0.5.0 - Shared Types & Utilities
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
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
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <commdlg.h>
#include <objbase.h>

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
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")
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
#include <algorithm>
#include <array>
#include <atomic>
#include <queue>
#include <fstream>

#include "deps/sqlite3.h"
#include "deps/json.hpp"
#include "deps/httplib.h"
#include "resource.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// Version & Config
// ============================================================================
static const char* APP_VERSION = "0.7.0";
static const char* HOST = "127.0.0.1";
static const int PORT = 8787;
static const int TOKEN_EXPIRY_HOURS = 24 * 30;

// ============================================================================
// Globals
// ============================================================================
inline fs::path g_data_dir;
inline fs::path g_db_path;
inline std::string g_jwt_secret;
inline std::atomic<bool> g_shutdown{false};
inline httplib::Server* g_server = nullptr;
inline HWND g_main_hwnd = nullptr;
inline NOTIFYICONDATAW g_nid = {};
inline bool g_portable_mode = false;

// Decrypted vault state
struct DecryptedEntry {
    std::string id;
    std::string name;
    std::string username;
    std::string password;
    std::string uri;
    std::string notes;
    std::string totp;
    std::string folderId;
    std::string folderName;
    int type = 1; // 1=Login, 2=SecureNote, 3=Card, 4=Identity
    bool favorite = false;
    std::string updatedAt;
};

struct DecryptedFolder {
    std::string id;
    std::string name;
};

struct VaultState {
    bool unlocked = false;
    std::string userId;
    std::string email;
    std::vector<uint8_t> userEncKey; // 32 bytes
    std::vector<uint8_t> userMacKey; // 32 bytes
    std::vector<DecryptedEntry> entries;
    std::vector<DecryptedFolder> folders;
    std::mutex mtx;

    void clear() {
        std::lock_guard<std::mutex> lk(mtx);
        if (!userEncKey.empty()) SecureZeroMemory(userEncKey.data(), userEncKey.size());
        if (!userMacKey.empty()) SecureZeroMemory(userMacKey.data(), userMacKey.size());
        userEncKey.clear();
        userMacKey.clear();
        entries.clear();
        folders.clear();
        unlocked = false;
        userId.clear();
        email.clear();
    }
};

inline VaultState g_vault;

// Log queue
inline std::mutex g_log_mtx;
inline std::queue<std::string> g_log_queue;

inline void vb_log(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &tt);
    char buf[128];
    snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        g_log_queue.push(std::string(buf) + msg);
    }
}

// ============================================================================
// Utility Functions
// ============================================================================
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64_encode(const uint8_t* data, size_t len) {
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

inline std::string base64_encode(const std::string& s) {
    return base64_encode((const uint8_t*)s.data(), s.size());
}

inline std::vector<uint8_t> base64_decode(const std::string& s) {
    static const auto tbl = []() {
        std::array<int, 256> t;
        t.fill(-1);
        for (int i = 0; i < 64; i++) t[(unsigned char)B64[i]] = i;
        return t;
    }();
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

inline std::string base64url_encode(const uint8_t* data, size_t len) {
    std::string b = base64_encode(data, len);
    for (auto& c : b) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
    while (!b.empty() && b.back() == '=') b.pop_back();
    return b;
}

inline std::string base64url_encode(const std::string& s) {
    return base64url_encode((const uint8_t*)s.data(), s.size());
}

inline std::vector<uint8_t> base64url_decode(const std::string& s) {
    std::string b = s;
    for (auto& c : b) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
    while (b.size() % 4) b += '=';
    return base64_decode(b);
}

inline std::string generate_uuid() {
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR str;
    UuidToStringA(&uuid, &str);
    std::string r((char*)str);
    RpcStringFreeA(&str);
    return r;
}

inline std::string generate_hex(int bytes = 32) {
    static const char hex[] = "0123456789abcdef";
    std::vector<uint8_t> buf(bytes);
    BCryptGenRandom(nullptr, buf.data(), (ULONG)bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::string r;
    r.reserve(bytes * 2);
    for (int i = 0; i < bytes; i++) {
        r += hex[buf[i] >> 4];
        r += hex[buf[i] & 0xF];
    }
    return r;
}

inline std::string utcnow() {
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm tm;
    gmtime_s(&tm, &tt);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

inline int64_t current_timestamp() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int64_t iso_to_ms(const std::string& iso) {
    struct tm tm = {};
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        return (int64_t)_mkgmtime(&tm) * 1000;
    }
    return current_timestamp() * 1000;
}

inline bool constant_time_eq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile uint8_t r = 0;
    for (size_t i = 0; i < a.size(); i++) r |= (uint8_t)(a[i] ^ b[i]);
    return r == 0;
}

inline std::string url_decode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v; if (sscanf(s.c_str() + i + 1, "%2x", &v) == 1) { r += (char)v; i += 2; continue; }
        } else if (s[i] == '+') { r += ' '; continue; }
        r += s[i];
    }
    return r;
}

inline std::map<std::string, std::string> parse_form(const std::string& body) {
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

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

inline std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

inline json parse_body(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    try { return json::parse(req.body); } catch (...) { return json::object(); }
}

inline int to_int(const json& v, int fallback = 0) {
    if (v.is_number()) return v.get<int>();
    if (v.is_string()) {
        auto s = v.get<std::string>();
        if (s.empty()) return fallback;
        try { return std::stoi(s); } catch (...) { return fallback; }
    }
    return fallback;
}

inline void send_json(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

inline void send_error(httplib::Response& res, int status, const std::string& msg) {
    res.status = status;
    res.set_content(json({{"message", msg}, {"Message", msg}}).dump(), "application/json");
}

// Wide string conversion helpers
inline std::wstring to_wstr(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

inline std::string from_wstr(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

// ============================================================================
// Start at Login (registry)
// ============================================================================
static const wchar_t* STARTUP_REG_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* STARTUP_REG_VALUE = L"VaultBox";

inline bool get_startup_enabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    bool exists = (RegQueryValueExW(hKey, STARTUP_REG_VALUE, nullptr, &type, nullptr, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return exists;
}

// ============================================================================
// Cloud Backup (copy vault.db to/from a cloud-synced folder)
// ============================================================================
static const wchar_t* BACKUP_REG_KEY = L"Software\\VaultBox";

inline std::string get_backup_path() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return "";
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof(buf), type = 0;
    if (RegQueryValueExW(hKey, L"BackupPath", nullptr, &type, (BYTE*)buf, &size) != ERROR_SUCCESS || type != REG_SZ)
        buf[0] = 0;
    RegCloseKey(hKey);
    return from_wstr(buf);
}

inline bool set_backup_path(const std::string& path) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;
    if (path.empty()) {
        RegDeleteValueW(hKey, L"BackupPath");
        RegDeleteValueW(hKey, L"AutoBackup");
    } else {
        std::wstring wp = to_wstr(path);
        RegSetValueExW(hKey, L"BackupPath", 0, REG_SZ, (const BYTE*)wp.c_str(), (DWORD)((wp.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
    return true;
}

inline bool get_auto_backup() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD val = 0, size = sizeof(val), type = 0;
    bool ok = (RegQueryValueExW(hKey, L"AutoBackup", nullptr, &type, (BYTE*)&val, &size) == ERROR_SUCCESS && val != 0);
    RegCloseKey(hKey);
    return ok;
}

inline bool set_auto_backup(bool enable) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;
    DWORD val = enable ? 1 : 0;
    RegSetValueExW(hKey, L"AutoBackup", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
    return true;
}

inline void wal_checkpoint() {
    sqlite3* db = nullptr;
    if (sqlite3_open(g_db_path.string().c_str(), &db) == SQLITE_OK) {
        sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
        sqlite3_close(db);
    }
}

inline json do_backup_now() {
    std::string backupDir = get_backup_path();
    if (backupDir.empty()) return {{"success", false}, {"error", "No backup folder configured"}};
    fs::path dest = fs::path(backupDir) / "vault.db";
    try {
        wal_checkpoint(); // Ensure all data is in the main db file
        fs::create_directories(backupDir);
        fs::copy_file(g_db_path, dest, fs::copy_options::overwrite_existing);
        vb_log("Vault backed up to " + dest.string());
        // Write timestamp
        auto now = utcnow();
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            std::wstring wts = to_wstr(now);
            RegSetValueExW(hKey, L"LastBackup", 0, REG_SZ, (const BYTE*)wts.c_str(), (DWORD)((wts.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
        return {{"success", true}, {"timestamp", now}, {"path", dest.string()}};
    } catch (const std::exception& e) {
        vb_log("Backup failed: " + std::string(e.what()));
        return {{"success", false}, {"error", e.what()}};
    }
}

inline json do_restore_from_backup() {
    std::string backupDir = get_backup_path();
    if (backupDir.empty()) return {{"success", false}, {"error", "No backup folder configured"}};
    fs::path src = fs::path(backupDir) / "vault.db";
    if (!fs::exists(src)) return {{"success", false}, {"error", "No vault.db found in backup folder"}};
    try {
        // Clear in-memory vault state first
        g_vault.clear();
        fs::copy_file(src, g_db_path, fs::copy_options::overwrite_existing);
        vb_log("Vault restored from " + src.string());
        return {{"success", true}, {"path", src.string()}};
    } catch (const std::exception& e) {
        vb_log("Restore failed: " + std::string(e.what()));
        return {{"success", false}, {"error", e.what()}};
    }
}

inline std::string get_last_backup_time() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return "";
    wchar_t buf[128] = {};
    DWORD size = sizeof(buf), type = 0;
    if (RegQueryValueExW(hKey, L"LastBackup", nullptr, &type, (BYTE*)buf, &size) != ERROR_SUCCESS)
        buf[0] = 0;
    RegCloseKey(hKey);
    return from_wstr(buf);
}

// Trigger auto-backup after vault changes (call from write endpoints)
inline void trigger_auto_backup() {
    if (get_auto_backup() && !get_backup_path().empty()) {
        std::thread([]() { do_backup_now(); }).detach();
    }
}

// Auto-lock timeout (minutes, 0 = disabled)
inline int get_autolock_minutes() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return 15; // default 15 minutes
    DWORD val = 15, size = sizeof(val), type = 0;
    if (RegQueryValueExW(hKey, L"AutoLockMinutes", nullptr, &type, (BYTE*)&val, &size) != ERROR_SUCCESS)
        val = 15;
    RegCloseKey(hKey);
    return (int)val;
}

inline void set_autolock_minutes(int minutes) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;
    DWORD val = (DWORD)minutes;
    RegSetValueExW(hKey, L"AutoLockMinutes", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
}

// Clipboard auto-clear seconds (0 = disabled)
inline int get_clipboard_clear_seconds() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return 30;
    DWORD val = 30, size = sizeof(val), type = 0;
    if (RegQueryValueExW(hKey, L"ClipboardClearSeconds", nullptr, &type, (BYTE*)&val, &size) != ERROR_SUCCESS)
        val = 30;
    RegCloseKey(hKey);
    return (int)val;
}

inline void set_clipboard_clear_seconds(int seconds) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, BACKUP_REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;
    DWORD val = (DWORD)seconds;
    RegSetValueExW(hKey, L"ClipboardClearSeconds", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
}

inline bool set_startup_enabled(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    bool ok;
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        ok = (RegSetValueExW(hKey, STARTUP_REG_VALUE, 0, REG_SZ,
            (const BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    } else {
        ok = (RegDeleteValueW(hKey, STARTUP_REG_VALUE) == ERROR_SUCCESS);
    }
    RegCloseKey(hKey);
    return ok;
}

