// VaultBox Desktop v0.4.0 - Shared Types & Utilities
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
static const char* APP_VERSION = "0.4.0";
static const char* HOST = "127.0.0.1";
static const int PORT = 8787;
static const int TOKEN_EXPIRY_HOURS = 24 * 30;

// ============================================================================
// Catppuccin Mocha Theme
// ============================================================================
namespace Theme {
    constexpr COLORREF Base      = RGB(30, 30, 46);
    constexpr COLORREF Mantle    = RGB(24, 24, 37);
    constexpr COLORREF Crust     = RGB(17, 17, 27);
    constexpr COLORREF Surface0  = RGB(49, 50, 68);
    constexpr COLORREF Surface1  = RGB(69, 71, 90);
    constexpr COLORREF Surface2  = RGB(88, 91, 112);
    constexpr COLORREF Overlay0  = RGB(108, 112, 134);
    constexpr COLORREF Overlay1  = RGB(127, 132, 156);
    constexpr COLORREF Text      = RGB(205, 214, 244);
    constexpr COLORREF Subtext0  = RGB(166, 173, 200);
    constexpr COLORREF Subtext1  = RGB(186, 194, 222);
    constexpr COLORREF Blue      = RGB(137, 180, 250);
    constexpr COLORREF Green     = RGB(166, 227, 161);
    constexpr COLORREF Red       = RGB(243, 139, 168);
    constexpr COLORREF Yellow    = RGB(249, 226, 175);
    constexpr COLORREF Peach     = RGB(250, 179, 135);
    constexpr COLORREF Mauve     = RGB(203, 166, 247);
    constexpr COLORREF Teal      = RGB(148, 226, 213);
    constexpr COLORREF Lavender  = RGB(180, 190, 254);
}

// ============================================================================
// Globals
// ============================================================================
inline fs::path g_data_dir;
inline fs::path g_db_path;
inline std::string g_jwt_secret;
inline std::atomic<bool> g_shutdown{false};
inline httplib::Server* g_server = nullptr;
inline HWND g_main_hwnd = nullptr;
inline HWND g_tray_hwnd = nullptr;
inline NOTIFYICONDATAW g_nid = {};
inline HFONT g_font = nullptr;
inline HFONT g_font_bold = nullptr;
inline HFONT g_font_mono = nullptr;
inline HFONT g_font_title = nullptr;    // Large title font for dialogs
inline HFONT g_font_detail = nullptr;   // Slightly larger for detail panel values

// Theme brushes
inline HBRUSH g_br_base = nullptr;
inline HBRUSH g_br_mantle = nullptr;
inline HBRUSH g_br_surface0 = nullptr;
inline HBRUSH g_br_surface1 = nullptr;
inline HBRUSH g_br_crust = nullptr;
inline HBRUSH g_br_blue = nullptr;
inline HBRUSH g_br_surface2 = nullptr;

// Decrypted vault state
struct DecryptedEntry {
    std::string id;
    std::string name;
    std::string username;
    std::string password;
    std::string uri;
    std::string notes;
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
    if (g_main_hwnd) PostMessage(g_main_hwnd, WM_VAULTBOX_LOG, 0, 0);
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

// Get text from a Win32 control
inline std::string get_ctrl_text(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return "";
    std::wstring buf(len + 1, 0);
    GetWindowTextW(hwnd, &buf[0], len + 1);
    buf.resize(len);
    return from_wstr(buf);
}

// Create themed fonts
inline void init_fonts() {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    // Base UI font - Segoe UI 10pt
    ncm.lfMessageFont.lfHeight = -15;
    ncm.lfMessageFont.lfWeight = FW_NORMAL;
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(ncm.lfMessageFont.lfFaceName, L"Segoe UI");
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    // Bold variant
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    g_font_bold = CreateFontIndirectW(&ncm.lfMessageFont);

    // Title font - large, for dialog headers
    LOGFONTW title = ncm.lfMessageFont;
    title.lfHeight = -28;
    title.lfWeight = FW_BOLD;
    title.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(title.lfFaceName, L"Segoe UI");
    g_font_title = CreateFontIndirectW(&title);

    // Detail value font - slightly larger for readability
    LOGFONTW detail = ncm.lfMessageFont;
    detail.lfHeight = -15;
    detail.lfWeight = FW_NORMAL;
    detail.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(detail.lfFaceName, L"Segoe UI");
    g_font_detail = CreateFontIndirectW(&detail);

    // Monospace font for passwords and log
    LOGFONTW mono = {};
    mono.lfHeight = -14;
    mono.lfWeight = FW_NORMAL;
    mono.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(mono.lfFaceName, L"Cascadia Mono");
    g_font_mono = CreateFontIndirectW(&mono);
    // Fallback to Consolas if Cascadia Mono unavailable
    if (!g_font_mono) {
        wcscpy_s(mono.lfFaceName, L"Consolas");
        g_font_mono = CreateFontIndirectW(&mono);
    }
}

// Create theme brushes
inline void init_brushes() {
    g_br_base = CreateSolidBrush(Theme::Base);
    g_br_mantle = CreateSolidBrush(Theme::Mantle);
    g_br_surface0 = CreateSolidBrush(Theme::Surface0);
    g_br_surface1 = CreateSolidBrush(Theme::Surface1);
    g_br_surface2 = CreateSolidBrush(Theme::Surface2);
    g_br_crust = CreateSolidBrush(Theme::Crust);
    g_br_blue = CreateSolidBrush(Theme::Blue);
}

inline void cleanup_gdi() {
    for (HFONT* f : {&g_font, &g_font_bold, &g_font_mono, &g_font_title, &g_font_detail})
        if (*f) { DeleteObject(*f); *f = nullptr; }
    for (HBRUSH* b : {&g_br_base, &g_br_mantle, &g_br_surface0, &g_br_surface1, &g_br_surface2, &g_br_crust, &g_br_blue})
        if (*b) { DeleteObject(*b); *b = nullptr; }
}
