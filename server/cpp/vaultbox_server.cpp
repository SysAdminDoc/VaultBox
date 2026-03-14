// VaultBox Desktop v0.5.0
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

    // Init data directory
    const char* custom = getenv("VAULTBOX_DATA");
    if (custom) {
        g_data_dir = fs::path(custom);
    } else {
        const char* la = getenv("LOCALAPPDATA");
        g_data_dir = fs::path(la ? la : ".") / "VaultBox";
    }
    g_db_path = g_data_dir / "vault.db";

    // Init database
    init_db();

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Start HTTP server on background thread
    std::thread serverThread([]() {
        httplib::Server svr;
        g_server = &svr;
        setup_routes(svr);
        vb_log("HTTP server started on http://127.0.0.1:" + std::to_string(PORT));
        svr.listen(HOST, PORT);
        vb_log("HTTP server stopped");
    });
    serverThread.detach();

    // Brief wait for server to start before WebView2 navigates to it
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Create main window (hidden initially, shown after WebView2 is ready)
    HWND hwnd = VBGUI::create_main_window(hInst);
    if (!hwnd) {
        CoUninitialize();
        CloseHandle(mutex);
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

    // Cleanup
    g_shutdown = true;
    if (g_server) g_server->stop();
    g_vault.clear();
    CloseHandle(mutex);
    CoUninitialize();
    return 0;
}
