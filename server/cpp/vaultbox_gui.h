// VaultBox Desktop - WebView2 GUI Host
// Manages the main window, WebView2 control, system tray icon, and app commands
#pragma once
#include "vaultbox_server.h"

#include <wrl.h>
#include "deps/webview2/WebView2.h"

using namespace Microsoft::WRL;

namespace VBGUI {

// WebView2 globals
inline ComPtr<ICoreWebView2Controller> g_webviewController;
inline ComPtr<ICoreWebView2> g_webview;
inline bool g_webview_ready = false;

// ============================================================================
// System Tray
// ============================================================================
inline void update_tray_tooltip() {
    std::wstring tip = L"VaultBox v" + to_wstr(APP_VERSION);
    if (g_portable_mode) tip += L" (Portable)";
    std::string lastBackup = get_last_backup_time();
    if (!lastBackup.empty()) {
        tip += L"\nBackup: " + to_wstr(lastBackup);
    }
    if (g_vault.unlocked) {
        tip += L"\nVault: unlocked (" + std::to_wstring(g_vault.entries.size()) + L" items)";
    } else {
        tip += L"\nVault: locked";
    }
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

inline void create_tray_icon(HWND hwnd) {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_VAULTBOX));
    wcscpy_s(g_nid.szTip, L"VaultBox Desktop");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    update_tray_tooltip();
}

inline void remove_tray_icon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

inline void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"Show VaultBox");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_QUIT, L"Quit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

// Forward declaration
inline void handle_app_command(HWND hwnd, const std::string& msg);

// ============================================================================
// WebView2 Initialization
// ============================================================================
inline void init_webview(HWND hwnd) {
    std::wstring userDataFolder = to_wstr((g_data_dir / "WebView2Cache").string());

    CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    vb_log("WebView2 environment creation failed");
                    MessageBoxW(hwnd, L"WebView2 runtime not found.\n\nPlease install the WebView2 Runtime from:\nhttps://developer.microsoft.com/en-us/microsoft-edge/webview2/",
                        L"VaultBox - WebView2 Required", MB_ICONERROR);
                    return S_OK;
                }

                env->CreateCoreWebView2Controller(hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) {
                                vb_log("WebView2 controller creation failed");
                                return S_OK;
                            }

                            g_webviewController = controller;
                            g_webviewController->get_CoreWebView2(&g_webview);

                            // Configure WebView2 settings
                            ComPtr<ICoreWebView2Settings> settings;
                            g_webview->get_Settings(&settings);
                            settings->put_IsScriptEnabled(TRUE);
                            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_AreDevToolsEnabled(FALSE);
                            settings->put_IsZoomControlEnabled(FALSE);

                            // Disable context menu
                            ComPtr<ICoreWebView2Settings3> settings3;
                            if (SUCCEEDED(settings.As(&settings3))) {
                                settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
                            }

                            // Set dark background color
                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(g_webviewController.As(&controller2))) {
                                COREWEBVIEW2_COLOR bg = { 255, 26, 26, 46 }; // #1a1a2e
                                controller2->put_DefaultBackgroundColor(bg);
                            }

                            // Resize WebView to fill window
                            RECT bounds;
                            GetClientRect(hwnd, &bounds);
                            g_webviewController->put_Bounds(bounds);
                            g_webviewController->put_IsVisible(TRUE);

                            // Handle postMessage from JavaScript
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [hwnd](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR msgRaw;
                                        args->TryGetWebMessageAsString(&msgRaw);
                                        if (msgRaw) {
                                            std::string msg = from_wstr(msgRaw);
                                            CoTaskMemFree(msgRaw);
                                            handle_app_command(hwnd, msg);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            // Navigate to local server
                            std::wstring url = L"http://127.0.0.1:" + std::to_wstring(PORT) + L"/";
                            g_webview->Navigate(url.c_str());
                            g_webview_ready = true;

                            vb_log("WebView2 initialized");

                            // Show the window now that WebView2 is ready
                            ShowWindow(hwnd, SW_SHOW);
                            UpdateWindow(hwnd);

                            return S_OK;
                        }).Get());

                return S_OK;
            }).Get());
}

// ============================================================================
// Handle app commands from JavaScript via postMessage
// ============================================================================
inline void handle_app_command(HWND hwnd, const std::string& msg) {
    try {
        auto j = json::parse(msg);
        std::string cmd = j.value("command", "");

        if (cmd == "minimize") {
            ShowWindow(hwnd, SW_HIDE);
        } else if (cmd == "quit") {
            PostMessage(hwnd, WM_VAULTBOX_QUIT, 0, 0);
        } else if (cmd == "opendata") {
            std::wstring wpath = to_wstr(g_data_dir.string());
            ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (cmd.rfind("launch:", 0) == 0) {
            std::string uri = cmd.substr(7);
            if (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0) {
                std::wstring wuri = to_wstr(uri);
                ShellExecuteW(nullptr, L"open", wuri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    } catch (...) {
        if (msg == "minimize") {
            ShowWindow(hwnd, SW_HIDE);
        } else if (msg == "quit") {
            PostMessage(hwnd, WM_VAULTBOX_QUIT, 0, 0);
        } else if (msg == "opendata") {
            std::wstring wpath = to_wstr(g_data_dir.string());
            ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (msg.rfind("launch:", 0) == 0) {
            std::string uri = msg.substr(7);
            if (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0) {
                std::wstring wuri = to_wstr(uri);
                ShellExecuteW(nullptr, L"open", wuri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    }
}

// ============================================================================
// Window Procedure
// ============================================================================
inline LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (g_webviewController) {
            RECT bounds;
            GetClientRect(hwnd, &bounds);
            g_webviewController->put_Bounds(bounds);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 800;
        mmi->ptMinTrackSize.y = 500;
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) update_tray_tooltip();
        return 0;

    case WM_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        } else if (lp == WM_RBUTTONUP) {
            show_tray_menu(hwnd);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TRAY_SHOW:
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return 0;
        case IDM_TRAY_QUIT:
            PostMessage(hwnd, WM_VAULTBOX_QUIT, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        // Hide to system tray on close (X button)
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE; // Allow Windows to shut down

    case WM_VAULTBOX_QUIT:
    case WM_ENDSESSION:
        remove_tray_icon();
        g_shutdown = true;
        if (g_webviewController) {
            g_webviewController->Close();
            g_webviewController = nullptr;
        }
        g_webview = nullptr;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
// Create Main Window
// ============================================================================
inline HWND create_main_window(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(26, 26, 46));
    wc.lpszClassName = L"VaultBoxDesktop";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_VAULTBOX));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_VAULTBOX));
    RegisterClassExW(&wc);

    BOOL dark = TRUE;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 1100, winH = 700;
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(0, L"VaultBoxDesktop",
        L"VaultBox Desktop",
        WS_OVERLAPPEDWINDOW,
        x, y, winW, winH,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) return nullptr;

    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    g_main_hwnd = hwnd;
    create_tray_icon(hwnd);
    SetTimer(hwnd, 1, 10000, nullptr); // Update tray tooltip every 10s

    return hwnd;
}

} // namespace VBGUI
