// VaultBox Desktop v0.4.0
// KeePass-style offline password manager with built-in Bitwarden-compatible server
// Single binary, zero runtime dependencies, fully offline
// Server: 127.0.0.1:8787 | GUI: Win32 + Catppuccin Mocha dark theme

#include "vaultbox_server.h"
#include "vaultbox_db.h"
#include "vaultbox_crypto.h"
#include "vaultbox_http.h"
#include "vaultbox_passgen.h"
#include "vaultbox_import.h"
#include "vaultbox_gui.h"

int main() {
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
        return 0;
    }

    // Init common controls
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

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

    // Init GDI resources
    init_fonts();
    init_brushes();

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

    // Create main window (hidden initially)
    HWND hwnd = VBGUI::create_main_window(hInst);
    if (!hwnd) {
        CloseHandle(mutex);
        return 1;
    }

    // Show unlock dialog
    vb_log("VaultBox Desktop v" + std::string(APP_VERSION) + " starting...");

    if (VBGUI::show_unlock_dialog(hwnd)) {
        // Vault unlocked - show main window with data
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        VBGUI::populate_tree();
        VBGUI::populate_list();
    } else {
        // No vault or user cancelled - still show window for server access
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }

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
    cleanup_gdi();
    CloseHandle(mutex);
    return 0;
}
