// VaultBox Desktop - Password Generator Dialog
#pragma once
#include "vaultbox_server.h"
#include "vaultbox_crypto.h"

namespace VBPassGen {

struct PassGenOptions {
    int length = 20;
    bool upper = true;
    bool lower = true;
    bool digits = true;
    bool symbols = true;
    bool ambiguous = false; // include ambiguous chars (0OIl1|)
};

inline std::string generate_password(const PassGenOptions& opts) {
    std::string charset;
    if (opts.lower) charset += "abcdefghijkmnopqrstuvwxyz";
    if (opts.upper) charset += "ABCDEFGHJKLMNPQRSTUVWXYZ";
    if (opts.digits) charset += "23456789";
    if (opts.symbols) charset += "!@#$%^&*()-_=+[]{}:;<>,.?/~";

    if (opts.ambiguous) {
        if (opts.lower) charset += "l";
        if (opts.upper) charset += "IO";
        if (opts.digits) charset += "01";
        charset += "|";
    }

    if (charset.empty()) charset = "abcdefghijkmnopqrstuvwxyz23456789";

    int len = std::max(4, std::min(128, opts.length));
    auto randbytes = VBCrypto::random_bytes(len);
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) {
        result += charset[randbytes[i] % charset.size()];
    }
    return result;
}

// Dialog state
static PassGenOptions s_opts;
static std::string s_preview;
static std::string* s_target = nullptr; // if non-null, put password here on "Use"

inline void update_preview(HWND dlg) {
    s_preview = generate_password(s_opts);
    SetWindowTextW(GetDlgItem(dlg, IDC_PG_PREVIEW), to_wstr(s_preview).c_str());
    // Update length label
    wchar_t buf[32];
    swprintf_s(buf, L"Length: %d", s_opts.length);
    SetWindowTextW(GetDlgItem(dlg, IDC_PG_LENGTH_LABEL), buf);
}

inline INT_PTR CALLBACK passgen_dlgproc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        // Dark theme
        BOOL dark = TRUE;
        DwmSetWindowAttribute(dlg, 20, &dark, sizeof(dark));

        // Init controls from options
        SendDlgItemMessageW(dlg, IDC_PG_LENGTH_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(4, 128));
        SendDlgItemMessageW(dlg, IDC_PG_LENGTH_SLIDER, TBM_SETPOS, TRUE, s_opts.length);

        CheckDlgButton(dlg, IDC_PG_UPPER, s_opts.upper ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_PG_LOWER, s_opts.lower ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_PG_DIGITS, s_opts.digits ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_PG_SYMBOLS, s_opts.symbols ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_PG_AMBIGUOUS, s_opts.ambiguous ? BST_CHECKED : BST_UNCHECKED);

        // Set fonts
        EnumChildWindows(dlg, [](HWND child, LPARAM) -> BOOL {
            SendMessage(child, WM_SETFONT, (WPARAM)g_font, TRUE);
            return TRUE;
        }, 0);
        SendDlgItemMessage(dlg, IDC_PG_PREVIEW, WM_SETFONT, (WPARAM)g_font_mono, TRUE);

        update_preview(dlg);

        // Show "Use" button only if we have a target
        if (!s_target) ShowWindow(GetDlgItem(dlg, IDC_PG_USE), SW_HIDE);

        return TRUE;
    }

    case WM_HSCROLL: {
        if ((HWND)lp == GetDlgItem(dlg, IDC_PG_LENGTH_SLIDER)) {
            s_opts.length = (int)SendDlgItemMessageW(dlg, IDC_PG_LENGTH_SLIDER, TBM_GETPOS, 0, 0);
            update_preview(dlg);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PG_UPPER:
        case IDC_PG_LOWER:
        case IDC_PG_DIGITS:
        case IDC_PG_SYMBOLS:
        case IDC_PG_AMBIGUOUS:
            s_opts.upper = IsDlgButtonChecked(dlg, IDC_PG_UPPER) == BST_CHECKED;
            s_opts.lower = IsDlgButtonChecked(dlg, IDC_PG_LOWER) == BST_CHECKED;
            s_opts.digits = IsDlgButtonChecked(dlg, IDC_PG_DIGITS) == BST_CHECKED;
            s_opts.symbols = IsDlgButtonChecked(dlg, IDC_PG_SYMBOLS) == BST_CHECKED;
            s_opts.ambiguous = IsDlgButtonChecked(dlg, IDC_PG_AMBIGUOUS) == BST_CHECKED;
            update_preview(dlg);
            return TRUE;

        case IDC_PG_GENERATE:
            update_preview(dlg);
            return TRUE;

        case IDC_PG_COPY:
            if (OpenClipboard(dlg)) {
                EmptyClipboard();
                size_t sz = s_preview.size() + 1;
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hg) {
                    memcpy(GlobalLock(hg), s_preview.c_str(), sz);
                    GlobalUnlock(hg);
                    SetClipboardData(CF_TEXT, hg);
                }
                CloseClipboard();
                // Auto-clear clipboard after 30 seconds
                SetTimer(dlg, 9999, 30000, [](HWND h, UINT, UINT_PTR id, DWORD) {
                    if (OpenClipboard(h)) { EmptyClipboard(); CloseClipboard(); }
                    KillTimer(h, id);
                });
            }
            return TRUE;

        case IDC_PG_USE:
            if (s_target) *s_target = s_preview;
            EndDialog(dlg, IDOK);
            return TRUE;

        case IDC_PG_OK:
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, Theme::Text);
        SetBkColor((HDC)wp, Theme::Base);
        return (INT_PTR)g_br_base;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, Theme::Text);
        SetBkColor((HDC)wp, Theme::Surface0);
        return (INT_PTR)g_br_surface0;

    case WM_CTLCOLORBTN:
        SetTextColor((HDC)wp, Theme::Text);
        SetBkColor((HDC)wp, Theme::Surface0);
        return (INT_PTR)g_br_surface0;
    }
    return FALSE;
}

// Create the dialog template in memory (no .rc file needed)
inline void show_passgen_dialog(HWND parent, std::string* target = nullptr) {
    s_target = target;

    // Build dialog template in memory
    // We'll create a simpler modeless approach using CreateWindowEx

    const int DLG_W = 380, DLG_H = 340;
    RECT parentRect;
    GetWindowRect(parent, &parentRect);
    int x = parentRect.left + (parentRect.right - parentRect.left - DLG_W) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - DLG_H) / 2;

    // Register a class for the dialog
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = g_br_base;
        wc.lpszClassName = L"VBPassGenDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"VBPassGenDlg", L"Password Generator",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, DLG_W, DLG_H, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(dlg, 20, &dark, sizeof(dark));

    int cy = 10;
    auto mkLabel = [&](const wchar_t* text, int cx, int w, int h) {
        HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
            cx, cy, w, h, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(hw, WM_SETFONT, (WPARAM)g_font, TRUE);
        return hw;
    };
    auto mkCheck = [&](const wchar_t* text, int id, int cx, int w, bool checked) {
        HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            cx, cy, w, 22, dlg, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        SendMessage(hw, WM_SETFONT, (WPARAM)g_font, TRUE);
        if (checked) SendMessage(hw, BM_SETCHECK, BST_CHECKED, 0);
        return hw;
    };
    auto mkButton = [&](const wchar_t* text, int id, int cx, int w, int h) {
        HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cx, cy, w, h, dlg, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        SendMessage(hw, WM_SETFONT, (WPARAM)g_font, TRUE);
        return hw;
    };

    // Preview
    mkLabel(L"Generated Password:", 15, 340, 18);
    cy += 22;
    HWND hPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
        15, cy, 250, 26, dlg, (HMENU)(INT_PTR)IDC_PG_PREVIEW, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hPreview, WM_SETFONT, (WPARAM)g_font_mono, TRUE);

    mkButton(L"Copy", IDC_PG_COPY, 270, 50, 26);
    mkButton(L"New", IDC_PG_GENERATE, 325, 35, 26);
    cy += 38;

    // Length slider
    HWND hLenLabel = CreateWindowExW(0, L"STATIC", L"Length: 20", WS_CHILD | WS_VISIBLE | SS_LEFT,
        15, cy, 120, 18, dlg, (HMENU)(INT_PTR)IDC_PG_LENGTH_LABEL, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hLenLabel, WM_SETFONT, (WPARAM)g_font, TRUE);
    cy += 22;

    HWND hSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
        15, cy, 340, 30, dlg, (HMENU)(INT_PTR)IDC_PG_LENGTH_SLIDER, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(4, 128));
    SendMessage(hSlider, TBM_SETPOS, TRUE, s_opts.length);
    SendMessage(hSlider, TBM_SETTICFREQ, 8, 0);
    cy += 40;

    // Checkboxes
    mkCheck(L"Uppercase (A-Z)", IDC_PG_UPPER, 15, 160, s_opts.upper);
    mkCheck(L"Lowercase (a-z)", IDC_PG_LOWER, 190, 160, s_opts.lower);
    cy += 26;
    mkCheck(L"Digits (0-9)", IDC_PG_DIGITS, 15, 160, s_opts.digits);
    mkCheck(L"Symbols (!@#$...)", IDC_PG_SYMBOLS, 190, 160, s_opts.symbols);
    cy += 26;
    mkCheck(L"Include ambiguous (0OIl1|)", IDC_PG_AMBIGUOUS, 15, 250, s_opts.ambiguous);
    cy += 36;

    // Buttons
    if (s_target) {
        mkButton(L"Use Password", IDC_PG_USE, 15, 110, 30);
        mkButton(L"Close", IDC_PG_OK, 290, 70, 30);
    } else {
        mkButton(L"Close", IDC_PG_OK, 290, 70, 30);
    }

    // Generate initial preview
    s_preview = generate_password(s_opts);
    SetWindowTextW(hPreview, to_wstr(s_preview).c_str());

    // Custom wndproc for dark theme and message handling
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)dlg);
    auto oldProc = (WNDPROC)SetWindowLongPtrW(dlg, GWLP_WNDPROC, (LONG_PTR)+[](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        switch (msg) {
        case WM_HSCROLL: {
            int pos = (int)SendDlgItemMessageW(hwnd, IDC_PG_LENGTH_SLIDER, TBM_GETPOS, 0, 0);
            s_opts.length = pos;
            s_preview = generate_password(s_opts);
            SetWindowTextW(GetDlgItem(hwnd, IDC_PG_PREVIEW), to_wstr(s_preview).c_str());
            wchar_t buf[32];
            swprintf_s(buf, L"Length: %d", pos);
            SetWindowTextW(GetDlgItem(hwnd, IDC_PG_LENGTH_LABEL), buf);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDC_PG_UPPER: case IDC_PG_LOWER: case IDC_PG_DIGITS:
            case IDC_PG_SYMBOLS: case IDC_PG_AMBIGUOUS:
                s_opts.upper = SendDlgItemMessage(hwnd, IDC_PG_UPPER, BM_GETCHECK, 0, 0) == BST_CHECKED;
                s_opts.lower = SendDlgItemMessage(hwnd, IDC_PG_LOWER, BM_GETCHECK, 0, 0) == BST_CHECKED;
                s_opts.digits = SendDlgItemMessage(hwnd, IDC_PG_DIGITS, BM_GETCHECK, 0, 0) == BST_CHECKED;
                s_opts.symbols = SendDlgItemMessage(hwnd, IDC_PG_SYMBOLS, BM_GETCHECK, 0, 0) == BST_CHECKED;
                s_opts.ambiguous = SendDlgItemMessage(hwnd, IDC_PG_AMBIGUOUS, BM_GETCHECK, 0, 0) == BST_CHECKED;
                s_preview = generate_password(s_opts);
                SetWindowTextW(GetDlgItem(hwnd, IDC_PG_PREVIEW), to_wstr(s_preview).c_str());
                return 0;
            case IDC_PG_GENERATE:
                s_preview = generate_password(s_opts);
                SetWindowTextW(GetDlgItem(hwnd, IDC_PG_PREVIEW), to_wstr(s_preview).c_str());
                return 0;
            case IDC_PG_COPY:
                if (OpenClipboard(hwnd)) {
                    EmptyClipboard();
                    size_t sz = s_preview.size() + 1;
                    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sz);
                    if (hg) { memcpy(GlobalLock(hg), s_preview.c_str(), sz); GlobalUnlock(hg); SetClipboardData(CF_TEXT, hg); }
                    CloseClipboard();
                    SetTimer(hwnd, 9999, 30000, [](HWND h, UINT, UINT_PTR id, DWORD) {
                        if (OpenClipboard(h)) { EmptyClipboard(); CloseClipboard(); }
                        KillTimer(h, id);
                    });
                }
                return 0;
            case IDC_PG_USE:
                if (s_target) *s_target = s_preview;
                DestroyWindow(hwnd);
                return 0;
            case IDC_PG_OK: case IDCANCEL:
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
            SetTextColor((HDC)wp, Theme::Text);
            SetBkColor((HDC)wp, Theme::Base);
            return (LRESULT)g_br_base;
        case WM_CTLCOLOREDIT:
            SetTextColor((HDC)wp, Theme::Text);
            SetBkColor((HDC)wp, Theme::Surface0);
            return (LRESULT)g_br_surface0;
        case WM_CTLCOLORBTN:
            SetTextColor((HDC)wp, Theme::Text);
            SetBkColor((HDC)wp, Theme::Surface0);
            return (LRESULT)g_br_surface0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            // Re-enable parent
            EnableWindow(GetParent(hwnd), TRUE);
            SetForegroundWindow(GetParent(hwnd));
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    });

    // Make modal-like
    EnableWindow(parent, FALSE);
    SetForegroundWindow(dlg);
}

} // namespace VBPassGen
