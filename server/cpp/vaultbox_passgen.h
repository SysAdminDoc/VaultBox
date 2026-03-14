// VaultBox Desktop - Password Generator Dialog (premium styled)
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
    bool ambiguous = false;
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
static std::string* s_target = nullptr;

// Forward declaration of button subclass from vaultbox_gui.h
// We'll use a local version here to avoid circular dependency
static LRESULT CALLBACK pg_btn_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR subId, DWORD_PTR refData) {
    bool isAccent = (refData == 1);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        bool hover = GetProp(hwnd, L"vb_hover") != nullptr;
        bool pressed = hover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
        bool disabled = !IsWindowEnabled(hwnd);

        COLORREF bgColor, textColor, borderColor;
        if (disabled) {
            bgColor = Theme::Surface0; textColor = Theme::Overlay0; borderColor = Theme::Surface1;
        } else if (isAccent) {
            bgColor = pressed ? RGB(107, 150, 220) : (hover ? RGB(157, 200, 255) : Theme::Blue);
            textColor = Theme::Crust;
            borderColor = pressed ? RGB(97, 140, 210) : Theme::Blue;
        } else {
            bgColor = pressed ? Theme::Surface2 : (hover ? Theme::Surface1 : Theme::Surface0);
            textColor = Theme::Text;
            borderColor = hover ? Theme::Overlay0 : Theme::Surface1;
        }

        HBRUSH bg = CreateSolidBrush(bgColor);
        HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, bg);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(bg);
        DeleteObject(pen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);
        SelectObject(hdc, g_font);
        wchar_t text[256] = {};
        GetWindowTextW(hwnd, text, 256);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!GetProp(hwnd, L"vb_hover")) {
            SetProp(hwnd, L"vb_hover", (HANDLE)1);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        RemoveProp(hwnd, L"vb_hover");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETTEXT:
    case WM_ENABLE:
        DefSubclassProc(hwnd, msg, wp, lp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return DefSubclassProc(hwnd, msg, wp, lp);
    case WM_LBUTTONUP:
        InvalidateRect(hwnd, nullptr, FALSE);
        return DefSubclassProc(hwnd, msg, wp, lp);
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        RemoveProp(hwnd, L"vb_hover");
        RemoveWindowSubclass(hwnd, pg_btn_subclass, subId);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static HWND pg_create_button(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h,
                              bool accent = false) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)g_font, TRUE);
    SetWindowSubclass(hw, pg_btn_subclass, (UINT_PTR)hw, accent ? 1 : 0);
    return hw;
}

inline void show_passgen_dialog(HWND parent, std::string* target = nullptr) {
    s_target = target;

    const int DLG_W = 400, DLG_H = 360;
    RECT parentRect;
    GetWindowRect(parent, &parentRect);
    int x = parentRect.left + (parentRect.right - parentRect.left - DLG_W) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - DLG_H) / 2;

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

    // Accent stripe
    HWND hAccent = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, DLG_W, 3, dlg, (HMENU)9999, GetModuleHandleW(nullptr), nullptr);

    int cy = 16;
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

    // Section header
    mkLabel(L"Generated Password", 20, 200, 18);
    cy += 24;

    // Preview field (monospace, larger)
    HWND hPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
        20, cy, 240, 28, dlg, (HMENU)(INT_PTR)IDC_PG_PREVIEW, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hPreview, WM_SETFONT, (WPARAM)g_font_mono, TRUE);

    pg_create_button(dlg, L"Copy", IDC_PG_COPY, 268, cy, 52, 28, true);
    pg_create_button(dlg, L"New", IDC_PG_GENERATE, 326, cy, 48, 28);
    cy += 42;

    // Length section
    HWND hLenLabel = CreateWindowExW(0, L"STATIC", L"Length: 20", WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, cy, 140, 18, dlg, (HMENU)(INT_PTR)IDC_PG_LENGTH_LABEL, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hLenLabel, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
    cy += 24;

    HWND hSlider = CreateWindowExW(0, TRACKBAR_CLASS, L"",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS,
        20, cy, 350, 30, dlg, (HMENU)(INT_PTR)IDC_PG_LENGTH_SLIDER, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(4, 128));
    SendMessage(hSlider, TBM_SETPOS, TRUE, s_opts.length);
    SendMessage(hSlider, TBM_SETTICFREQ, 8, 0);
    cy += 42;

    // Character options (grid layout)
    mkLabel(L"Character Sets", 20, 200, 18);
    cy += 24;
    mkCheck(L"Uppercase (A-Z)", IDC_PG_UPPER, 20, 170, s_opts.upper);
    mkCheck(L"Lowercase (a-z)", IDC_PG_LOWER, 200, 170, s_opts.lower);
    cy += 28;
    mkCheck(L"Digits (0-9)", IDC_PG_DIGITS, 20, 170, s_opts.digits);
    mkCheck(L"Symbols (!@#$...)", IDC_PG_SYMBOLS, 200, 170, s_opts.symbols);
    cy += 28;
    mkCheck(L"Include ambiguous (0OIl1|)", IDC_PG_AMBIGUOUS, 20, 260, s_opts.ambiguous);
    cy += 40;

    // Bottom buttons
    if (s_target) {
        pg_create_button(dlg, L"Use Password", IDC_PG_USE, 20, cy, 120, 32, true);
    }
    pg_create_button(dlg, L"Close", IDC_PG_OK, 300, cy, 74, 32);

    // Generate initial preview
    s_preview = generate_password(s_opts);
    SetWindowTextW(hPreview, to_wstr(s_preview).c_str());

    // Custom wndproc for message handling + dark theme
    SetWindowLongPtrW(dlg, GWLP_WNDPROC, (LONG_PTR)+[](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
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
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
            if (dis->CtlID == 9999) {
                HBRUSH accent = CreateSolidBrush(Theme::Blue);
                FillRect(dis->hDC, &dis->rcItem, accent);
                DeleteObject(accent);
                return TRUE;
            }
            break;
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
                    std::wstring w = to_wstr(s_preview);
                    size_t sz = (w.size() + 1) * sizeof(wchar_t);
                    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sz);
                    if (hg) { memcpy(GlobalLock(hg), w.c_str(), sz); GlobalUnlock(hg); SetClipboardData(CF_UNICODETEXT, hg); }
                    CloseClipboard();
                    SetTimer(hwnd, 9999, 30000, [](HWND h, UINT, UINT_PTR id, DWORD) {
                        if (OpenClipboard(h)) { EmptyClipboard(); CloseClipboard(); }
                        KillTimer(h, id);
                    });
                }
                return 0;
            case IDC_PG_USE:
                if (s_target) *s_target = s_preview;
                EnableWindow(GetParent(hwnd), TRUE);
                SetForegroundWindow(GetParent(hwnd));
                DestroyWindow(hwnd);
                return 0;
            case IDC_PG_OK: case IDCANCEL:
                EnableWindow(GetParent(hwnd), TRUE);
                SetForegroundWindow(GetParent(hwnd));
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CTLCOLORDLG:
            return (LRESULT)g_br_base;
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, Theme::Text);
            SetBkColor(hdc, Theme::Base);
            return (LRESULT)g_br_base;
        }
        case WM_CTLCOLOREDIT:
            SetTextColor((HDC)wp, Theme::Text);
            SetBkColor((HDC)wp, Theme::Surface0);
            return (LRESULT)g_br_surface0;
        case WM_CTLCOLORBTN:
            return (LRESULT)g_br_base;
        case WM_CLOSE:
            EnableWindow(GetParent(hwnd), TRUE);
            SetForegroundWindow(GetParent(hwnd));
            DestroyWindow(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    });

    EnableWindow(parent, FALSE);
    SetForegroundWindow(dlg);
}

} // namespace VBPassGen
