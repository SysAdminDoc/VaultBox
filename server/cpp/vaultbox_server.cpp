// VaultBox Desktop v0.9.0
// Bitwarden-compatible offline password manager with WebView2 GUI
// Single binary, zero runtime dependencies (except WebView2 runtime), fully offline
// Server: 127.0.0.1:8787 | GUI: WebView2 + Bitwarden dark theme

#include "vaultbox_server.h"
#include "vaultbox_db.h"
#include "vaultbox_crypto.h"
#include "vaultbox_passgen.h"
#include "vaultbox_import.h"
#include "vaultbox_http.h"
#include "vaultbox_gui.h"

int main() {
    // Initialize COM for WebView2
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Single-instance mutex
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Global\\VaultBoxServerMutex");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        // Try to find and show existing window
        HWND existing = FindWindowW(L"VaultBoxDesktop", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CoUninitialize();
        return 0;
    }

    // Init data directory (portable mode: vault.db next to exe takes priority)
    const char* custom = getenv("VAULTBOX_DATA");
    if (custom) {
        g_data_dir = fs::path(custom);
    } else {
        // Check for portable mode: vault.db next to the executable
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        if (fs::exists(exeDir / "vault.db") || fs::exists(exeDir / "vaultbox.portable")) {
            g_data_dir = exeDir;
            g_portable_mode = true;
        } else {
            const char* la = getenv("LOCALAPPDATA");
            g_data_dir = fs::path(la ? la : ".") / "VaultBox";
        }
    }
    g_db_path = g_data_dir / "vault.db";

    // Init database (abort visibly if the data directory is unusable, e.g. read-only drive).
    try {
        init_db();
    } catch (const std::exception& e) {
        std::wstring msg = L"VaultBox failed to initialize its database at:\n"
                         + to_wstr(g_db_path.string()) + L"\n\n"
                         + to_wstr(e.what());
        MessageBoxW(nullptr, msg.c_str(), L"VaultBox - Startup Error", MB_ICONERROR);
        CloseHandle(mutex);
        CoUninitialize();
        return 2;
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Bind the server socket synchronously so we can detect "port already in use"
    // before creating the window. If bind fails (stale instance, another app on 8787,
    // firewall blocking loopback), show the user an actionable error instead of a
    // blank WebView2 window that never loads.
    httplib::Server svr;
    g_server = &svr;
    setup_routes(svr);
    if (!svr.bind_to_port(HOST, PORT)) {
        std::wstring msg = L"VaultBox could not bind to 127.0.0.1:"
                         + std::to_wstring(PORT) + L".\n\n"
                         L"Another application is likely using this port. Close any previous "
                         L"VaultBox instance and try again.";
        MessageBoxW(nullptr, msg.c_str(), L"VaultBox - Port In Use", MB_ICONERROR);
        g_server = nullptr;
        CloseHandle(mutex);
        CoUninitialize();
        return 3;
    }

    std::thread serverThread([&svr]() {
        vb_log("HTTP server started on http://127.0.0.1:" + std::to_string(PORT));
        svr.listen_after_bind();
        vb_log("HTTP server stopped");
    });

    // Server-side auto-lock watchdog. Acts as a backstop when the SPA JS timer is
    // paused (minimised, backgrounded, DevTools detached), which Windows/WebView2
    // may throttle. If no vault-touching activity has occurred for the configured
    // number of minutes, clear the vault from memory.
    touch_activity();
    std::thread autoLockThread([]() {
        while (!g_shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (g_shutdown.load()) break;
            int mins = get_autolock_minutes();
            if (mins <= 0) continue; // disabled
            if (!g_vault.unlocked) continue;
            int64_t idle = monotonic_seconds() - g_last_activity_s.load();
            if (idle >= (int64_t)mins * 60) {
                g_vault.clear();
                vb_log("Auto-lock: vault locked after " + std::to_string(mins) + " min of inactivity");
            }
        }
    });
    autoLockThread.detach();

    // Create main window (hidden initially, shown after WebView2 is ready)
    HWND hwnd = VBGUI::create_main_window(hInst);
    if (!hwnd) {
        svr.stop();
        if (serverThread.joinable()) serverThread.join();
        g_server = nullptr;
        CloseHandle(mutex);
        CoUninitialize();
        return 1;
    }

    vb_log("VaultBox Desktop v" + std::string(APP_VERSION) + " starting...");

    // Initialize WebView2 (async - window will be shown when ready)
    VBGUI::init_webview(hwnd);

    // Main message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup: stop accepting new requests, wait for in-flight requests to drain,
    // then wipe any decrypted material from memory before exit.
    g_shutdown = true;
    svr.stop();
    if (serverThread.joinable()) serverThread.join();
    g_server = nullptr;
    g_vault.clear();
    CloseHandle(mutex);
    CoUninitialize();
    return 0;
}
