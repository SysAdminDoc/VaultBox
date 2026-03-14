// VaultBox Desktop - Win32 GUI (3-pane KeePass-style layout with Catppuccin Mocha dark theme)
// Premium owner-drawn controls, custom-draw ListView/TreeView, polished dialogs
#pragma once
#include "vaultbox_server.h"
#include "vaultbox_db.h"
#include "vaultbox_crypto.h"
#include "vaultbox_passgen.h"
#include "vaultbox_import.h"

namespace VBGUI {

// Layout constants
static const int TREE_WIDTH = 220;
static const int DETAIL_HEIGHT = 140;
static const int LOG_HEIGHT = 100;
static const int STATUSBAR_HEIGHT = 24;
static const int LIST_ROW_HEIGHT = 28;
static const int SEPARATOR_WIDTH = 1;

// GUI state
static HWND g_hTree = nullptr;
static HWND g_hList = nullptr;
static HWND g_hDetail = nullptr;
static HWND g_hLog = nullptr;
static HWND g_hStatus = nullptr;
static bool g_log_visible = true;
static int g_tree_width = TREE_WIDTH;
static int g_selected_entry_idx = -1;
static std::string g_filter_folder_id;
static int g_filter_type = -1;

// TreeView item types
enum TreeItemType { TI_ALL, TI_LOGIN, TI_CARD, TI_IDENTITY, TI_NOTE, TI_FAVORITES, TI_FOLDER };
struct TreeItemData {
    TreeItemType type;
    std::string folderId;
};
static std::vector<TreeItemData> g_tree_items;

// ============================================================================
// Owner-Drawn Button Subclass (premium rounded buttons with hover/press)
// ============================================================================
// Data stored per-button: 1 = accent/primary, 0 = normal
static LRESULT CALLBACK vb_btn_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
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
        bool focused = (GetFocus() == hwnd);
        bool disabled = !(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_VISIBLE) ||
                        !(IsWindowEnabled(hwnd));

        // Choose colors
        COLORREF bgColor, textColor, borderColor;
        if (disabled) {
            bgColor = Theme::Surface0;
            textColor = Theme::Overlay0;
            borderColor = Theme::Surface1;
        } else if (isAccent) {
            bgColor = pressed ? RGB(107, 150, 220) : (hover ? RGB(157, 200, 255) : Theme::Blue);
            textColor = Theme::Crust;
            borderColor = pressed ? RGB(97, 140, 210) : Theme::Blue;
        } else {
            bgColor = pressed ? Theme::Surface2 : (hover ? Theme::Surface1 : Theme::Surface0);
            textColor = Theme::Text;
            borderColor = hover ? Theme::Overlay0 : Theme::Surface1;
        }

        // Draw rounded background
        HBRUSH bg = CreateSolidBrush(bgColor);
        HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, bg);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(bg);
        DeleteObject(pen);

        // Focus ring (subtle)
        if (focused && !isAccent) {
            HPEN focusPen = CreatePen(PS_SOLID, 1, Theme::Blue);
            SelectObject(hdc, focusPen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            DeleteObject(focusPen);
        }

        // Draw text
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
        // Repaint after text/state change
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
        return 1; // We handle everything in WM_PAINT

    case WM_NCDESTROY:
        RemoveProp(hwnd, L"vb_hover");
        RemoveWindowSubclass(hwnd, vb_btn_subclass, subId);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Helper to apply premium button styling
static void style_button(HWND btn, bool accent = false) {
    SetWindowSubclass(btn, vb_btn_subclass, (UINT_PTR)btn, accent ? 1 : 0);
}

// Helper to create a styled button
static HWND create_button(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h,
                           bool accent = false) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)g_font, TRUE);
    style_button(hw, accent);
    return hw;
}

// ============================================================================
// Custom-drawn separator line
// ============================================================================
static void paint_separator(HDC hdc, int x, int y, int w, int h, bool vertical = false) {
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::Surface1);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    if (vertical) {
        MoveToEx(hdc, x, y, nullptr);
        LineTo(hdc, x, y + h);
    } else {
        MoveToEx(hdc, x, y, nullptr);
        LineTo(hdc, x + w, y);
    }
    SelectObject(hdc, old);
    DeleteObject(pen);
}

// ============================================================================
// Detail Panel
// ============================================================================
struct DetailControls {
    HWND nameLbl, nameVal;
    HWND userLbl, userVal, userCopy;
    HWND passLbl, passVal, passCopy, passShow;
    HWND uriLbl, uriVal, uriLaunch;
    bool passVisible = false;
    std::string currentPassword;
};
static DetailControls g_det;

static void create_detail_panel(HWND parent) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    auto mkLabel = [&](int x, int y, int w, int h, const wchar_t* text) {
        HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, y, w, h, parent, nullptr, hInst, nullptr);
        SendMessage(hw, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
        return hw;
    };
    auto mkValue = [&](int x, int y, int w, int h) {
        HWND hw = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
            x, y, w, h, parent, nullptr, hInst, nullptr);
        SendMessage(hw, WM_SETFONT, (WPARAM)g_font_detail, TRUE);
        return hw;
    };

    int y = 12, labelW = 80, rowH = 24, gap = 6, btnW = 56, btnH = 24;
    int valX = 92;

    g_det.nameLbl = mkLabel(14, y, labelW, rowH, L"Name");
    g_det.nameVal = mkValue(valX, y, 300, rowH);
    y += rowH + gap;

    g_det.userLbl = mkLabel(14, y, labelW, rowH, L"Username");
    g_det.userVal = mkValue(valX, y, 300 - btnW - 6, rowH);
    g_det.userCopy = create_button(parent, L"Copy", IDC_DET_USER_COPY,
        valX + 300 - btnW, y, btnW, btnH);
    y += rowH + gap;

    g_det.passLbl = mkLabel(14, y, labelW, rowH, L"Password");
    g_det.passVal = mkValue(valX, y, 300 - 2 * btnW - 12, rowH);
    SendMessage(g_det.passVal, WM_SETFONT, (WPARAM)g_font_mono, TRUE);
    g_det.passShow = create_button(parent, L"Show", IDC_DET_PASS_SHOW,
        valX + 300 - 2 * btnW - 4, y, btnW, btnH);
    g_det.passCopy = create_button(parent, L"Copy", IDC_DET_PASS_COPY,
        valX + 300 - btnW, y, btnW, btnH);
    y += rowH + gap;

    g_det.uriLbl = mkLabel(14, y, labelW, rowH, L"URI");
    g_det.uriVal = mkValue(valX, y, 300 - btnW - 6, rowH);
    g_det.uriLaunch = create_button(parent, L"Open", IDC_DET_URI_LAUNCH,
        valX + 300 - btnW, y, btnW, btnH, true);
}

static void update_detail(int idx) {
    g_selected_entry_idx = idx;
    g_det.passVisible = false;

    if (idx < 0 || idx >= (int)g_vault.entries.size()) {
        SetWindowTextW(g_det.nameVal, L"");
        SetWindowTextW(g_det.userVal, L"");
        SetWindowTextW(g_det.passVal, L"");
        SetWindowTextW(g_det.uriVal, L"");
        g_det.currentPassword.clear();
        return;
    }

    auto& e = g_vault.entries[idx];
    SetWindowTextW(g_det.nameVal, to_wstr(e.name).c_str());
    SetWindowTextW(g_det.userVal, to_wstr(e.username).c_str());
    SetWindowTextW(g_det.passVal, L"\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022");
    SetWindowTextW(g_det.uriVal, to_wstr(e.uri).c_str());
    g_det.currentPassword = e.password;
    SetWindowTextW(g_det.passShow, L"Show");
}

static void copy_to_clipboard(HWND hwnd, const std::string& text) {
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        std::wstring w = to_wstr(text);
        size_t sz = (w.size() + 1) * sizeof(wchar_t);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sz);
        if (hg) {
            memcpy(GlobalLock(hg), w.c_str(), sz);
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
        CloseClipboard();
        SetTimer(hwnd, 8888, 30000, [](HWND h, UINT, UINT_PTR id, DWORD) {
            if (OpenClipboard(h)) { EmptyClipboard(); CloseClipboard(); }
            KillTimer(h, id);
        });
    }
}

// ============================================================================
// TreeView Population (with Unicode category icons)
// ============================================================================
static void populate_tree() {
    TreeView_DeleteAllItems(g_hTree);
    g_tree_items.clear();

    TVINSERTSTRUCT tvis = {};
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;

    auto addItem = [&](const wchar_t* text, TreeItemType type, const std::string& folderId = "") {
        g_tree_items.push_back({type, folderId});
        tvis.item.lParam = (LPARAM)(g_tree_items.size() - 1);
        tvis.item.pszText = (LPWSTR)text;
        HTREEITEM h = TreeView_InsertItem(g_hTree, &tvis);
        return h;
    };

    HTREEITEM hAll = addItem(L"\x25C9  All Items", TI_ALL);
    addItem(L"\x2605  Favorites", TI_FAVORITES);
    addItem(L"\x2022  Logins", TI_LOGIN);
    addItem(L"\x25A0  Cards", TI_CARD);
    addItem(L"\x25CB  Identities", TI_IDENTITY);
    addItem(L"\x25A1  Secure Notes", TI_NOTE);

    for (auto& f : g_vault.folders) {
        std::wstring wname = L"\x25B8  " + to_wstr(f.name);
        addItem(wname.c_str(), TI_FOLDER, f.id);
    }

    TreeView_SelectItem(g_hTree, hAll);
}

// ============================================================================
// ListView Population
// ============================================================================
static std::vector<int> g_visible_indices;

static void populate_list() {
    ListView_DeleteAllItems(g_hList);
    g_visible_indices.clear();

    for (int i = 0; i < (int)g_vault.entries.size(); i++) {
        auto& e = g_vault.entries[i];

        if (g_filter_type == 0) {
            if (!e.favorite) continue;
        } else if (g_filter_type > 0) {
            if (e.type != g_filter_type) continue;
        }
        if (!g_filter_folder_id.empty()) {
            if (e.folderId != g_filter_folder_id) continue;
        }

        g_visible_indices.push_back(i);

        int row = ListView_GetItemCount(g_hList);
        LVITEM lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = row;
        lvi.lParam = i;

        std::wstring wname = to_wstr(e.name);
        lvi.pszText = (LPWSTR)wname.c_str();
        ListView_InsertItem(g_hList, &lvi);

        std::wstring wuser = to_wstr(e.username);
        ListView_SetItemText(g_hList, row, 1, (LPWSTR)wuser.c_str());

        std::wstring wuri = to_wstr(e.uri);
        ListView_SetItemText(g_hList, row, 2, (LPWSTR)wuri.c_str());

        const wchar_t* typeName = L"Login";
        if (e.type == 2) typeName = L"Note";
        else if (e.type == 3) typeName = L"Card";
        else if (e.type == 4) typeName = L"Identity";
        ListView_SetItemText(g_hList, row, 3, (LPWSTR)typeName);
    }

    wchar_t buf[64];
    swprintf_s(buf, L" %d entries", (int)g_visible_indices.size());
    SetWindowTextW(g_hStatus, buf);

    update_detail(-1);
}

// ============================================================================
// Get selected entry index in vault
// ============================================================================
static int get_selected_vault_index() {
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)g_visible_indices.size()) return -1;
    return g_visible_indices[sel];
}

// ============================================================================
// Entry Add/Edit Dialog (premium styled)
// ============================================================================
static DecryptedEntry s_edit_entry;
static bool s_edit_is_new = false;

static LRESULT CALLBACK entry_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

        HINSTANCE hInst = GetModuleHandleW(nullptr);
        int y = 16, labelW = 80, fieldW = 280, rowH = 26, gap = 8;
        int fieldX = 100;

        auto mkLabel = [&](const wchar_t* text) {
            HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                12, y + 3, labelW, rowH, hwnd, nullptr, hInst, nullptr);
            SendMessage(h, WM_SETFONT, (WPARAM)g_font, TRUE);
        };
        auto mkEdit = [&](int id, DWORD style = 0) {
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP | style,
                fieldX, y, fieldW, rowH, hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
            SendMessage(h, WM_SETFONT, (WPARAM)g_font, TRUE);
            return h;
        };

        // Header accent line
        HWND hAccent = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 500, 3, hwnd, (HMENU)9999, hInst, nullptr);

        mkLabel(L"Name"); mkEdit(IDC_ENTRY_NAME); y += rowH + gap;
        mkLabel(L"Username"); mkEdit(IDC_ENTRY_USERNAME); y += rowH + gap;
        mkLabel(L"Password"); mkEdit(IDC_ENTRY_PASSWORD, ES_PASSWORD);

        create_button(hwnd, L"Show", IDC_ENTRY_SHOW_PASS,
            fieldX + fieldW - 112, y + rowH + 4, 52, 24);
        create_button(hwnd, L"Generate", IDC_ENTRY_GEN_PASS,
            fieldX + fieldW - 56, y + rowH + 4, 56, 24, true);
        y += rowH + gap + 30;

        mkLabel(L"URI"); mkEdit(IDC_ENTRY_URI); y += rowH + gap;

        mkLabel(L"Type");
        HWND hType = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
            fieldX, y, fieldW, 200, hwnd, (HMENU)(INT_PTR)IDC_ENTRY_TYPE, hInst, nullptr);
        SendMessage(hType, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"Login");
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"Secure Note");
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"Card");
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"Identity");
        int typeIdx = 0;
        if (s_edit_entry.type == 2) typeIdx = 1;
        else if (s_edit_entry.type == 3) typeIdx = 2;
        else if (s_edit_entry.type == 4) typeIdx = 3;
        SendMessage(hType, CB_SETCURSEL, typeIdx, 0);
        y += rowH + gap;

        mkLabel(L"Folder");
        HWND hFolder = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
            fieldX, y, fieldW, 200, hwnd, (HMENU)(INT_PTR)IDC_ENTRY_FOLDER, hInst, nullptr);
        SendMessage(hFolder, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(hFolder, CB_ADDSTRING, 0, (LPARAM)L"(No Folder)");
        int folderSel = 0;
        for (int i = 0; i < (int)g_vault.folders.size(); i++) {
            SendMessageW(hFolder, CB_ADDSTRING, 0, (LPARAM)to_wstr(g_vault.folders[i].name).c_str());
            if (g_vault.folders[i].id == s_edit_entry.folderId) folderSel = i + 1;
        }
        SendMessage(hFolder, CB_SETCURSEL, folderSel, 0);
        y += rowH + gap;

        mkLabel(L"Notes");
        HWND hNotes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
            fieldX, y, fieldW, 70, hwnd, (HMENU)(INT_PTR)IDC_ENTRY_NOTES, hInst, nullptr);
        SendMessage(hNotes, WM_SETFONT, (WPARAM)g_font, TRUE);
        y += 70 + gap + 10;

        create_button(hwnd, L"Save", IDC_ENTRY_OK,
            fieldX + fieldW - 160, y, 74, 30, true);
        create_button(hwnd, L"Cancel", IDC_ENTRY_CANCEL,
            fieldX + fieldW - 78, y, 74, 30);

        // Populate fields
        SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_NAME), to_wstr(s_edit_entry.name).c_str());
        SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_USERNAME), to_wstr(s_edit_entry.username).c_str());
        SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_PASSWORD), to_wstr(s_edit_entry.password).c_str());
        SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_URI), to_wstr(s_edit_entry.uri).c_str());
        SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_NOTES), to_wstr(s_edit_entry.notes).c_str());

        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlID == 9999) {
            // Accent header line
            HBRUSH accent = CreateSolidBrush(Theme::Blue);
            FillRect(dis->hDC, &dis->rcItem, accent);
            DeleteObject(accent);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_ENTRY_SHOW_PASS: {
            HWND hPw = GetDlgItem(hwnd, IDC_ENTRY_PASSWORD);
            LONG_PTR style = GetWindowLongPtrW(hPw, GWL_STYLE);
            if (style & ES_PASSWORD) {
                SetWindowLongPtrW(hPw, GWL_STYLE, style & ~ES_PASSWORD);
                SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_SHOW_PASS), L"Hide");
            } else {
                SetWindowLongPtrW(hPw, GWL_STYLE, style | ES_PASSWORD);
                SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_SHOW_PASS), L"Show");
            }
            std::string pw = get_ctrl_text(hPw);
            SetWindowTextW(hPw, to_wstr(pw).c_str());
            return 0;
        }
        case IDC_ENTRY_GEN_PASS: {
            std::string gen = VBPassGen::generate_password(VBPassGen::s_opts);
            SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_PASSWORD), to_wstr(gen).c_str());
            // Show the password after generating
            HWND hPw = GetDlgItem(hwnd, IDC_ENTRY_PASSWORD);
            LONG_PTR style = GetWindowLongPtrW(hPw, GWL_STYLE);
            SetWindowLongPtrW(hPw, GWL_STYLE, style & ~ES_PASSWORD);
            SetWindowTextW(GetDlgItem(hwnd, IDC_ENTRY_SHOW_PASS), L"Hide");
            SetWindowTextW(hPw, to_wstr(gen).c_str());
            return 0;
        }
        case IDC_ENTRY_OK: {
            s_edit_entry.name = get_ctrl_text(GetDlgItem(hwnd, IDC_ENTRY_NAME));
            s_edit_entry.username = get_ctrl_text(GetDlgItem(hwnd, IDC_ENTRY_USERNAME));
            s_edit_entry.password = get_ctrl_text(GetDlgItem(hwnd, IDC_ENTRY_PASSWORD));
            s_edit_entry.uri = get_ctrl_text(GetDlgItem(hwnd, IDC_ENTRY_URI));
            s_edit_entry.notes = get_ctrl_text(GetDlgItem(hwnd, IDC_ENTRY_NOTES));

            int ti = (int)SendDlgItemMessage(hwnd, IDC_ENTRY_TYPE, CB_GETCURSEL, 0, 0);
            int types[] = {1, 2, 3, 4};
            s_edit_entry.type = (ti >= 0 && ti < 4) ? types[ti] : 1;

            int fi = (int)SendDlgItemMessage(hwnd, IDC_ENTRY_FOLDER, CB_GETCURSEL, 0, 0);
            s_edit_entry.folderId = "";
            if (fi > 0 && fi <= (int)g_vault.folders.size())
                s_edit_entry.folderId = g_vault.folders[fi - 1].id;

            if (VBCrypto::save_entry(s_edit_entry, s_edit_is_new)) {
                VBCrypto::refresh_vault();
                PostMessage(g_main_hwnd, WM_VAULTBOX_REFRESH, 0, 0);
                vb_log(s_edit_is_new ? "Entry created: " + s_edit_entry.name : "Entry updated: " + s_edit_entry.name);
            }

            EnableWindow(GetParent(hwnd), TRUE);
            DestroyWindow(hwnd);
            return 0;
        }
        case IDC_ENTRY_CANCEL:
        case IDCANCEL:
            EnableWindow(GetParent(hwnd), TRUE);
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
        return (LRESULT)g_br_base;
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp, Theme::Text);
        SetBkColor((HDC)wp, Theme::Surface0);
        return (LRESULT)g_br_surface0;
    case WM_CLOSE:
        EnableWindow(GetParent(hwnd), TRUE);
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_entry_dialog(HWND parent, bool isNew, int vaultIdx = -1) {
    s_edit_is_new = isNew;
    if (isNew) {
        s_edit_entry = DecryptedEntry();
    } else if (vaultIdx >= 0 && vaultIdx < (int)g_vault.entries.size()) {
        s_edit_entry = g_vault.entries[vaultIdx];
    } else {
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = entry_dlg_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = g_br_base;
        wc.lpszClassName = L"VBEntryDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    int DLG_W = 440, DLG_H = 500;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + (pr.right - pr.left - DLG_W) / 2;
    int y = pr.top + (pr.bottom - pr.top - DLG_H) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"VBEntryDlg",
        isNew ? L"Add Entry" : L"Edit Entry",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, DLG_W, DLG_H, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    EnableWindow(parent, FALSE);
}

// ============================================================================
// Folder Dialog (premium styled)
// ============================================================================
static std::string s_folder_name;
static std::string s_folder_id;

static void show_folder_dialog(HWND parent, const std::string& existingId = "", const std::string& existingName = "") {
    s_folder_id = existingId;
    s_folder_name = existingName;

    int DLG_W = 360, DLG_H = 160;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + (pr.right - pr.left - DLG_W) / 2;
    int y = pr.top + (pr.bottom - pr.top - DLG_H) / 2;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            switch (msg) {
            case WM_CREATE: {
                BOOL dark = TRUE;
                DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
                HINSTANCE hInst = GetModuleHandleW(nullptr);

                // Accent line
                HWND hAccent = CreateWindowExW(0, L"STATIC", L"",
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    0, 0, 400, 3, hwnd, (HMENU)9999, hInst, nullptr);

                HWND hLbl = CreateWindowExW(0, L"STATIC", L"Folder Name", WS_CHILD | WS_VISIBLE,
                    16, 16, 100, 22, hwnd, nullptr, hInst, nullptr);
                HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", to_wstr(s_folder_name).c_str(),
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                    16, 42, 310, 26, hwnd, (HMENU)(INT_PTR)IDC_FOLDER_NAME, hInst, nullptr);

                auto hOk = create_button(hwnd, L"Save", IDC_FOLDER_OK, 168, 82, 74, 30, true);
                auto hCancel = create_button(hwnd, L"Cancel", IDC_FOLDER_CANCEL, 250, 82, 74, 30);

                for (HWND h : {hLbl, hEdit})
                    SendMessage(h, WM_SETFONT, (WPARAM)g_font, TRUE);
                SetFocus(hEdit);
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
                if (LOWORD(wp) == IDC_FOLDER_OK) {
                    std::string name = get_ctrl_text(GetDlgItem(hwnd, IDC_FOLDER_NAME));
                    if (!name.empty()) {
                        VBCrypto::save_folder(name, s_folder_id);
                        VBCrypto::refresh_vault();
                        PostMessage(g_main_hwnd, WM_VAULTBOX_REFRESH, 0, 0);
                        vb_log(s_folder_id.empty() ? "Folder created: " + name : "Folder renamed: " + name);
                    }
                    EnableWindow(GetParent(hwnd), TRUE);
                    DestroyWindow(hwnd);
                } else if (LOWORD(wp) == IDC_FOLDER_CANCEL || LOWORD(wp) == IDCANCEL) {
                    EnableWindow(GetParent(hwnd), TRUE);
                    DestroyWindow(hwnd);
                }
                return 0;
            case WM_CTLCOLORDLG: case WM_CTLCOLORSTATIC:
                SetTextColor((HDC)wp, Theme::Text); SetBkColor((HDC)wp, Theme::Base); return (LRESULT)g_br_base;
            case WM_CTLCOLOREDIT:
                SetTextColor((HDC)wp, Theme::Text); SetBkColor((HDC)wp, Theme::Surface0); return (LRESULT)g_br_surface0;
            case WM_CTLCOLORBTN:
                return (LRESULT)g_br_base;
            case WM_CLOSE:
                EnableWindow(GetParent(hwnd), TRUE); DestroyWindow(hwnd); return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = g_br_base;
        wc.lpszClassName = L"VBFolderDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"VBFolderDlg",
        existingId.empty() ? L"New Folder" : L"Rename Folder",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, DLG_W, DLG_H, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(parent, FALSE);
}

// ============================================================================
// Premium Unlock Dialog
// ============================================================================
static bool show_unlock_dialog(HWND parent) {
    DB db;
    auto accounts = db.query("SELECT email FROM accounts LIMIT 1");
    std::string defaultEmail = accounts.empty() ? "" : accounts[0]["email"].get<std::string>();

    if (accounts.empty()) {
        vb_log("No vault found. Create one using the browser extension, then restart.");
        MessageBoxW(parent, L"No vault found.\n\nCreate one using the VaultBox browser extension, then restart this application.",
            L"VaultBox", MB_ICONINFORMATION | MB_OK);
        return false;
    }

    static std::string s_email, s_password;
    static bool s_result = false;
    s_email = defaultEmail;
    s_password.clear();
    s_result = false;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            switch (msg) {
            case WM_CREATE: {
                BOOL dark = TRUE;
                DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
                HINSTANCE hInst = GetModuleHandleW(nullptr);

                // Accent stripe at top
                CreateWindowExW(0, L"STATIC", L"",
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    0, 0, 500, 4, hwnd, (HMENU)9999, hInst, nullptr);

                // Lock icon (large Unicode)
                HWND hIcon = CreateWindowExW(0, L"STATIC", L"\x2616",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    0, 18, 440, 36, hwnd, nullptr, hInst, nullptr);
                SendMessage(hIcon, WM_SETFONT, (WPARAM)g_font_title, TRUE);

                // Title
                HWND hTitle = CreateWindowExW(0, L"STATIC", L"VaultBox",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    0, 50, 440, 36, hwnd, (HMENU)9998, hInst, nullptr);
                SendMessage(hTitle, WM_SETFONT, (WPARAM)g_font_title, TRUE);

                // Subtitle
                HWND hSub = CreateWindowExW(0, L"STATIC", L"Enter your master password to unlock the vault",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    0, 84, 440, 20, hwnd, (HMENU)9997, hInst, nullptr);
                SendMessage(hSub, WM_SETFONT, (WPARAM)g_font, TRUE);

                int y = 120;

                // Email field
                HWND hEmailLbl = CreateWindowExW(0, L"STATIC", L"Email", WS_CHILD | WS_VISIBLE | SS_LEFT,
                    60, y, 60, 22, hwnd, nullptr, hInst, nullptr);
                SendMessage(hEmailLbl, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
                y += 24;

                HWND hEmail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", to_wstr(s_email).c_str(),
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                    60, y, 320, 28, hwnd, (HMENU)(INT_PTR)IDC_UNLOCK_EMAIL, hInst, nullptr);
                SendMessage(hEmail, WM_SETFONT, (WPARAM)g_font, TRUE);
                y += 40;

                // Password field
                HWND hPassLbl = CreateWindowExW(0, L"STATIC", L"Master Password", WS_CHILD | WS_VISIBLE | SS_LEFT,
                    60, y, 200, 22, hwnd, nullptr, hInst, nullptr);
                SendMessage(hPassLbl, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
                y += 24;

                HWND hPass = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
                    60, y, 320, 28, hwnd, (HMENU)(INT_PTR)IDC_UNLOCK_PASSWORD, hInst, nullptr);
                SendMessage(hPass, WM_SETFONT, (WPARAM)g_font, TRUE);
                y += 44;

                // Unlock button (centered, accent)
                create_button(hwnd, L"Unlock Vault", IDC_UNLOCK_OK,
                    145, y, 150, 36, true);

                // Version text at bottom
                HWND hVer = CreateWindowExW(0, L"STATIC",
                    L"VaultBox Desktop v0.4.0  \x2022  Offline  \x2022  AES-256",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    0, y + 50, 440, 18, hwnd, (HMENU)9996, hInst, nullptr);
                SendMessage(hVer, WM_SETFONT, (WPARAM)g_font, TRUE);

                SetFocus(hPass);
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
                if (LOWORD(wp) == IDC_UNLOCK_OK) {
                    s_email = get_ctrl_text(GetDlgItem(hwnd, IDC_UNLOCK_EMAIL));
                    s_password = get_ctrl_text(GetDlgItem(hwnd, IDC_UNLOCK_PASSWORD));

                    SetWindowTextW(GetDlgItem(hwnd, IDC_UNLOCK_OK), L"Unlocking...");
                    EnableWindow(GetDlgItem(hwnd, IDC_UNLOCK_OK), FALSE);
                    UpdateWindow(hwnd);

                    std::thread([hwnd]() {
                        bool ok = VBCrypto::unlock_vault(s_password, s_email);
                        SecureZeroMemory(&s_password[0], s_password.size());
                        PostMessage(hwnd, WM_USER + 100, ok ? 1 : 0, 0);
                    }).detach();
                    return 0;
                }
                if (LOWORD(wp) == IDCANCEL) {
                    PostQuitMessage(0);
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;

            case WM_USER + 100:
                if (wp == 1) {
                    s_result = true;
                    DestroyWindow(hwnd);
                } else {
                    MessageBoxW(hwnd, L"Wrong master password.", L"Unlock Failed", MB_ICONERROR | MB_OK);
                    SetWindowTextW(GetDlgItem(hwnd, IDC_UNLOCK_OK), L"Unlock Vault");
                    EnableWindow(GetDlgItem(hwnd, IDC_UNLOCK_OK), TRUE);
                    SetFocus(GetDlgItem(hwnd, IDC_UNLOCK_PASSWORD));
                    SetWindowTextW(GetDlgItem(hwnd, IDC_UNLOCK_PASSWORD), L"");
                }
                return 0;

            case WM_CTLCOLORDLG:
                return (LRESULT)g_br_base;
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                SetBkColor(hdc, Theme::Base);
                // Title text in accent color
                HWND ctrl = (HWND)lp;
                int id = GetDlgCtrlID(ctrl);
                if (id == 9998) {
                    SetTextColor(hdc, Theme::Blue);
                } else if (id == 9996 || id == 9997) {
                    SetTextColor(hdc, Theme::Subtext0);
                } else {
                    SetTextColor(hdc, Theme::Text);
                }
                return (LRESULT)g_br_base;
            }
            case WM_CTLCOLOREDIT:
                SetTextColor((HDC)wp, Theme::Text);
                SetBkColor((HDC)wp, Theme::Surface0);
                return (LRESULT)g_br_surface0;
            case WM_CTLCOLORBTN:
                return (LRESULT)g_br_base;
            case WM_CLOSE:
                PostQuitMessage(0);
                DestroyWindow(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = g_br_base;
        wc.lpszClassName = L"VBUnlockDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    int DLG_W = 440, DLG_H = 380;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"VBUnlockDlg", L"VaultBox",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        (sx - DLG_W) / 2, (sy - DLG_H) / 2, DLG_W, DLG_H,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsWindow(dlg)) break;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            PostMessage(dlg, WM_COMMAND, IDC_UNLOCK_OK, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return s_result;
}

// ============================================================================
// Custom About Dialog
// ============================================================================
static void show_about_dialog(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            switch (msg) {
            case WM_CREATE: {
                BOOL dark = TRUE;
                DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
                HINSTANCE hInst = GetModuleHandleW(nullptr);

                // Accent stripe
                CreateWindowExW(0, L"STATIC", L"",
                    WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                    0, 0, 420, 4, hwnd, (HMENU)9999, hInst, nullptr);

                // Lock icon
                HWND hIcon = CreateWindowExW(0, L"STATIC", L"\x2616",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    0, 16, 420, 36, hwnd, nullptr, hInst, nullptr);
                SendMessage(hIcon, WM_SETFONT, (WPARAM)g_font_title, TRUE);

                // Title
                HWND hTitle = CreateWindowExW(0, L"STATIC", L"VaultBox Desktop",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    0, 52, 420, 30, hwnd, (HMENU)9998, hInst, nullptr);
                SendMessage(hTitle, WM_SETFONT, (WPARAM)g_font_title, TRUE);

                // Version
                HWND hVer = CreateWindowExW(0, L"STATIC", L"Version 0.4.0",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    0, 82, 420, 20, hwnd, (HMENU)9997, hInst, nullptr);
                SendMessage(hVer, WM_SETFONT, (WPARAM)g_font, TRUE);

                // Info lines
                const wchar_t* lines[] = {
                    L"Offline password manager with KeePass-style interface",
                    L"Built-in Bitwarden-compatible server on 127.0.0.1:8787",
                    L"",
                    L"Encryption: AES-256-CBC + HMAC-SHA256",
                    L"Key Derivation: PBKDF2-SHA256 (600,000 iterations)",
                    L"Random: BCryptGenRandom (CSPRNG)",
                    L"",
                    L"Your vault never leaves this device."
                };
                int y = 116;
                for (auto line : lines) {
                    HWND h = CreateWindowExW(0, L"STATIC", line,
                        WS_CHILD | WS_VISIBLE | SS_CENTER,
                        20, y, 380, 18, hwnd, nullptr, hInst, nullptr);
                    SendMessage(h, WM_SETFONT, (WPARAM)g_font, TRUE);
                    y += 20;
                }

                create_button(hwnd, L"Close", IDCANCEL, 170, y + 10, 80, 30, true);
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
                if (LOWORD(wp) == IDCANCEL) {
                    EnableWindow(GetParent(hwnd), TRUE);
                    DestroyWindow(hwnd);
                }
                return 0;
            case WM_CTLCOLORDLG:
                return (LRESULT)g_br_base;
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                SetBkColor(hdc, Theme::Base);
                int id = GetDlgCtrlID((HWND)lp);
                if (id == 9998)
                    SetTextColor(hdc, Theme::Blue);
                else if (id == 9997)
                    SetTextColor(hdc, Theme::Subtext0);
                else
                    SetTextColor(hdc, Theme::Subtext1);
                return (LRESULT)g_br_base;
            }
            case WM_CTLCOLORBTN:
                return (LRESULT)g_br_base;
            case WM_CLOSE:
                EnableWindow(GetParent(hwnd), TRUE);
                DestroyWindow(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = g_br_base;
        wc.lpszClassName = L"VBAboutDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    int DLG_W = 420, DLG_H = 360;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + (pr.right - pr.left - DLG_W) / 2;
    int y = pr.top + (pr.bottom - pr.top - DLG_H) / 2;

    CreateWindowExW(WS_EX_DLGMODALFRAME, L"VBAboutDlg", L"About VaultBox",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, DLG_W, DLG_H, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(parent, FALSE);
}

// ============================================================================
// Menu Bar
// ============================================================================
static HMENU create_menu_bar() {
    HMENU bar = CreateMenu();

    HMENU file = CreatePopupMenu();
    HMENU importMenu = CreatePopupMenu();
    AppendMenuW(importMenu, MF_STRING, IDM_FILE_IMPORT_BW_JSON, L"Bitwarden JSON...");
    AppendMenuW(importMenu, MF_STRING, IDM_FILE_IMPORT_BW_CSV, L"Bitwarden CSV...");
    AppendMenuW(importMenu, MF_STRING, IDM_FILE_IMPORT_CHROME, L"Chrome CSV...");
    AppendMenuW(importMenu, MF_STRING, IDM_FILE_IMPORT_KEEPASS, L"KeePass XML...");

    HMENU exportMenu = CreatePopupMenu();
    AppendMenuW(exportMenu, MF_STRING, IDM_FILE_EXPORT_JSON, L"Bitwarden JSON...");
    AppendMenuW(exportMenu, MF_STRING, IDM_FILE_EXPORT_CSV, L"CSV...");

    AppendMenuW(file, MF_POPUP, (UINT_PTR)importMenu, L"Import");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)exportMenu, L"Export");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_FILE_LOCK, L"Lock Vault");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_FILE_QUIT, L"Quit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"File");

    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, IDM_EDIT_ADD_ENTRY, L"Add Entry");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_EDIT_ENTRY, L"Edit Entry");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_DELETE_ENTRY, L"Delete Entry");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_EDIT_COPY_USER, L"Copy Username");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_COPY_PASS, L"Copy Password");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_LAUNCH_URI, L"Launch URI");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_EDIT_ADD_FOLDER, L"Add Folder");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_RENAME_FOLDER, L"Rename Folder");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_DELETE_FOLDER, L"Delete Folder");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"Edit");

    HMENU tools = CreatePopupMenu();
    AppendMenuW(tools, MF_STRING, IDM_TOOLS_PASSGEN, L"Password Generator");
    AppendMenuW(tools, MF_STRING, IDM_TOOLS_OPEN_DATA, L"Open Data Folder");
    AppendMenuW(tools, MF_STRING, IDM_TOOLS_TOGGLE_LOG, L"Toggle Log Panel");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"Tools");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, L"About VaultBox");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"Help");

    return bar;
}

// ============================================================================
// System Tray (improved icon - VB monogram with accent)
// ============================================================================
static HICON create_tray_icon() {
    int sz = 32;
    HDC hdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, sz, sz);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    // Dark background with rounded feel
    HBRUSH bg = CreateSolidBrush(Theme::Crust);
    RECT rc = {0, 0, sz, sz};
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    // Blue accent fill (vault body)
    HBRUSH blue = CreateSolidBrush(Theme::Blue);
    RECT vault = {4, 12, 28, 28};
    HPEN noPen = CreatePen(PS_SOLID, 1, Theme::Blue);
    SelectObject(mem, blue);
    SelectObject(mem, noPen);
    RoundRect(mem, vault.left, vault.top, vault.right, vault.bottom, 4, 4);
    DeleteObject(blue);
    DeleteObject(noPen);

    // Lock shackle (arc at top)
    HPEN shackle = CreatePen(PS_SOLID, 3, Theme::Lavender);
    SelectObject(mem, shackle);
    SelectObject(mem, GetStockObject(NULL_BRUSH));
    Arc(mem, 10, 2, 22, 16, 22, 9, 10, 9);
    DeleteObject(shackle);

    // Keyhole dot
    HBRUSH dark = CreateSolidBrush(Theme::Crust);
    SelectObject(mem, dark);
    SelectObject(mem, GetStockObject(NULL_PEN));
    Ellipse(mem, 13, 17, 19, 23);
    DeleteObject(dark);

    SelectObject(mem, oldBmp);

    HDC mdc = CreateCompatibleDC(hdc);
    HGDIOBJ oldMask = SelectObject(mdc, mask);
    RECT mrc = {0, 0, sz, sz};
    FillRect(mdc, &mrc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(mdc, oldMask);
    DeleteDC(mdc);
    DeleteDC(mem);
    ReleaseDC(nullptr, hdc);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = bmp;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    DeleteObject(mask);
    return icon;
}

static void setup_tray(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = create_tray_icon();
    wcscpy_s(g_nid.szTip, L"VaultBox Desktop");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// ============================================================================
// Layout (with painted separators)
// ============================================================================
static void layout_controls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    int logH = g_log_visible ? LOG_HEIGHT : 0;
    int mainH = h - logH - STATUSBAR_HEIGHT;
    int rightW = w - g_tree_width - SEPARATOR_WIDTH;
    int listH = mainH - DETAIL_HEIGHT - SEPARATOR_WIDTH;

    if (g_hTree) MoveWindow(g_hTree, 0, 0, g_tree_width, mainH, TRUE);
    if (g_hList) MoveWindow(g_hList, g_tree_width + SEPARATOR_WIDTH, 0, rightW, listH, TRUE);
    if (g_hDetail) MoveWindow(g_hDetail, g_tree_width + SEPARATOR_WIDTH, listH + SEPARATOR_WIDTH, rightW, DETAIL_HEIGHT, TRUE);
    if (g_hLog) {
        MoveWindow(g_hLog, 0, mainH, w, logH, TRUE);
        ShowWindow(g_hLog, g_log_visible ? SW_SHOW : SW_HIDE);
    }
    if (g_hStatus) MoveWindow(g_hStatus, 0, h - STATUSBAR_HEIGHT, w, STATUSBAR_HEIGHT, TRUE);
}

// ============================================================================
// Main Window Procedure
// ============================================================================
static LRESULT CALLBACK main_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_main_hwnd = hwnd;
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        // Dark title bar
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

        // Create TreeView
        g_hTree = CreateWindowExW(0, WC_TREEVIEW, L"",
            WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT,
            0, 0, TREE_WIDTH, 400, hwnd, (HMENU)(INT_PTR)IDC_TREEVIEW, hInst, nullptr);
        SendMessage(g_hTree, WM_SETFONT, (WPARAM)g_font, TRUE);
        SetWindowTheme(g_hTree, L"DarkMode_Explorer", nullptr);
        TreeView_SetBkColor(g_hTree, Theme::Mantle);
        TreeView_SetTextColor(g_hTree, Theme::Text);
        TreeView_SetItemHeight(g_hTree, 28);

        // Create ListView
        g_hList = CreateWindowExW(0, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            TREE_WIDTH + 2, 0, 600, 300, hwnd, (HMENU)(INT_PTR)IDC_LISTVIEW, hInst, nullptr);
        SendMessage(g_hList, WM_SETFONT, (WPARAM)g_font, TRUE);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        SetWindowTheme(g_hList, L"DarkMode_Explorer", nullptr);
        ListView_SetBkColor(g_hList, Theme::Base);
        ListView_SetTextColor(g_hList, Theme::Text);
        ListView_SetTextBkColor(g_hList, Theme::Base);

        // ListView columns
        LVCOLUMN col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPWSTR)L"Name"; col.cx = 220; ListView_InsertColumn(g_hList, 0, &col);
        col.pszText = (LPWSTR)L"Username"; col.cx = 200; ListView_InsertColumn(g_hList, 1, &col);
        col.pszText = (LPWSTR)L"URI"; col.cx = 220; ListView_InsertColumn(g_hList, 2, &col);
        col.pszText = (LPWSTR)L"Type"; col.cx = 80; ListView_InsertColumn(g_hList, 3, &col);

        // Detail panel (container with mantle background for card effect)
        g_hDetail = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            TREE_WIDTH + 2, 300, 600, DETAIL_HEIGHT, hwnd, (HMENU)(INT_PTR)IDC_DETAIL_PANEL, hInst, nullptr);
        create_detail_panel(g_hDetail);

        // Log panel
        g_hLog = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 400, 800, LOG_HEIGHT, hwnd, (HMENU)(INT_PTR)IDC_LOG_PANEL, hInst, nullptr);
        SendMessage(g_hLog, WM_SETFONT, (WPARAM)g_font_mono, TRUE);

        // Status bar (custom styled)
        g_hStatus = CreateWindowExW(0, L"STATIC", L" Ready",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 500, 800, STATUSBAR_HEIGHT, hwnd, (HMENU)(INT_PTR)IDC_STATUSBAR, hInst, nullptr);
        SendMessage(g_hStatus, WM_SETFONT, (WPARAM)g_font, TRUE);

        setup_tray(hwnd);
        SetMenu(hwnd, create_menu_bar());
        layout_controls(hwnd);
        return 0;
    }

    case WM_SIZE:
        layout_controls(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 750;
        mmi->ptMinTrackSize.y = 520;
        return 0;
    }

    case WM_VAULTBOX_LOG: {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        while (!g_log_queue.empty()) {
            std::string msg = g_log_queue.front();
            g_log_queue.pop();
            int len = GetWindowTextLengthW(g_hLog);
            SendMessage(g_hLog, EM_SETSEL, len, len);
            std::wstring wMsg = to_wstr(msg + "\r\n");
            SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)wMsg.c_str());
        }
        return 0;
    }

    case WM_VAULTBOX_REFRESH:
        populate_tree();
        populate_list();
        return 0;

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;

        // TreeView selection
        if (nm->hwndFrom == g_hTree && nm->code == TVN_SELCHANGED) {
            NMTREEVIEW* ntv = (NMTREEVIEW*)lp;
            int idx = (int)ntv->itemNew.lParam;
            if (idx >= 0 && idx < (int)g_tree_items.size()) {
                auto& ti = g_tree_items[idx];
                g_filter_folder_id.clear();
                g_filter_type = -1;
                switch (ti.type) {
                case TI_ALL: break;
                case TI_FAVORITES: g_filter_type = 0; break;
                case TI_LOGIN: g_filter_type = 1; break;
                case TI_CARD: g_filter_type = 3; break;
                case TI_IDENTITY: g_filter_type = 4; break;
                case TI_NOTE: g_filter_type = 2; break;
                case TI_FOLDER: g_filter_folder_id = ti.folderId; break;
                }
                populate_list();
            }
        }

        // ListView events
        if (nm->hwndFrom == g_hList) {
            if (nm->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* nlv = (NMLISTVIEW*)lp;
                if (nlv->uNewState & LVIS_SELECTED) {
                    int vIdx = get_selected_vault_index();
                    update_detail(vIdx);
                }
            }
            if (nm->code == NM_DBLCLK) {
                int vIdx = get_selected_vault_index();
                if (vIdx >= 0) show_entry_dialog(hwnd, false, vIdx);
            }

            // ListView custom draw - alternating rows + selection color
            if (nm->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int row = (int)cd->nmcd.dwItemSpec;
                    bool selected = (ListView_GetItemState(g_hList, row, LVIS_SELECTED) & LVIS_SELECTED);
                    if (selected) {
                        cd->clrTextBk = RGB(45, 55, 85); // Subtle blue selection
                        cd->clrText = Theme::Text;
                    } else if (row % 2 == 1) {
                        cd->clrTextBk = RGB(35, 35, 52); // Slightly lighter alternating
                        cd->clrText = Theme::Text;
                    } else {
                        cd->clrTextBk = Theme::Base;
                        cd->clrText = Theme::Text;
                    }
                    return CDRF_NEWFONT;
                }
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        int cmd = LOWORD(wp);

        // Detail panel button commands
        if (cmd == IDC_DET_USER_COPY && g_selected_entry_idx >= 0) {
            copy_to_clipboard(hwnd, g_vault.entries[g_selected_entry_idx].username);
            return 0;
        }
        if (cmd == IDC_DET_PASS_COPY && g_selected_entry_idx >= 0) {
            copy_to_clipboard(hwnd, g_vault.entries[g_selected_entry_idx].password);
            return 0;
        }
        if (cmd == IDC_DET_PASS_SHOW && g_selected_entry_idx >= 0) {
            g_det.passVisible = !g_det.passVisible;
            if (g_det.passVisible) {
                SetWindowTextW(g_det.passVal, to_wstr(g_det.currentPassword).c_str());
                SetWindowTextW(g_det.passShow, L"Hide");
            } else {
                SetWindowTextW(g_det.passVal, L"\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022\x2022");
                SetWindowTextW(g_det.passShow, L"Show");
            }
            return 0;
        }
        if (cmd == IDC_DET_URI_LAUNCH && g_selected_entry_idx >= 0) {
            auto& uri = g_vault.entries[g_selected_entry_idx].uri;
            if (!uri.empty()) ShellExecuteW(nullptr, L"open", to_wstr(uri).c_str(), nullptr, nullptr, SW_SHOW);
            return 0;
        }

        // Menu commands
        switch (cmd) {
        case IDM_FILE_IMPORT_BW_JSON: {
            auto f = VBImport::open_file_dialog(hwnd, L"JSON Files\0*.json\0All Files\0*.*\0", L"Import Bitwarden JSON");
            if (!f.empty()) { VBImport::import_bitwarden_json(f); PostMessage(hwnd, WM_VAULTBOX_REFRESH, 0, 0); }
            return 0;
        }
        case IDM_FILE_IMPORT_BW_CSV: {
            auto f = VBImport::open_file_dialog(hwnd, L"CSV Files\0*.csv\0All Files\0*.*\0", L"Import Bitwarden CSV");
            if (!f.empty()) { VBImport::import_bitwarden_csv(f); PostMessage(hwnd, WM_VAULTBOX_REFRESH, 0, 0); }
            return 0;
        }
        case IDM_FILE_IMPORT_CHROME: {
            auto f = VBImport::open_file_dialog(hwnd, L"CSV Files\0*.csv\0All Files\0*.*\0", L"Import Chrome CSV");
            if (!f.empty()) { VBImport::import_chrome_csv(f); PostMessage(hwnd, WM_VAULTBOX_REFRESH, 0, 0); }
            return 0;
        }
        case IDM_FILE_IMPORT_KEEPASS: {
            auto f = VBImport::open_file_dialog(hwnd, L"XML Files\0*.xml\0All Files\0*.*\0", L"Import KeePass XML");
            if (!f.empty()) { VBImport::import_keepass_xml(f); PostMessage(hwnd, WM_VAULTBOX_REFRESH, 0, 0); }
            return 0;
        }
        case IDM_FILE_EXPORT_JSON: {
            auto f = VBImport::save_file_dialog(hwnd, L"JSON Files\0*.json\0", L"Export Bitwarden JSON", L"json");
            if (!f.empty()) VBImport::export_bitwarden_json(f);
            return 0;
        }
        case IDM_FILE_EXPORT_CSV: {
            auto f = VBImport::save_file_dialog(hwnd, L"CSV Files\0*.csv\0", L"Export CSV", L"csv");
            if (!f.empty()) VBImport::export_csv(f);
            return 0;
        }
        case IDM_FILE_LOCK:
            g_vault.clear();
            TreeView_DeleteAllItems(g_hTree);
            ListView_DeleteAllItems(g_hList);
            update_detail(-1);
            vb_log("Vault locked");
            if (show_unlock_dialog(hwnd)) {
                populate_tree();
                populate_list();
            }
            return 0;
        case IDM_FILE_QUIT:
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_EDIT_ADD_ENTRY:
            show_entry_dialog(hwnd, true);
            return 0;
        case IDM_EDIT_EDIT_ENTRY: {
            int vIdx = get_selected_vault_index();
            if (vIdx >= 0) show_entry_dialog(hwnd, false, vIdx);
            return 0;
        }
        case IDM_EDIT_DELETE_ENTRY: {
            int vIdx = get_selected_vault_index();
            if (vIdx >= 0) {
                std::string name = g_vault.entries[vIdx].name;
                VBCrypto::delete_entry(g_vault.entries[vIdx].id);
                VBCrypto::refresh_vault();
                PostMessage(hwnd, WM_VAULTBOX_REFRESH, 0, 0);
                vb_log("Entry deleted: " + name);
            }
            return 0;
        }
        case IDM_EDIT_COPY_USER: {
            int vIdx = get_selected_vault_index();
            if (vIdx >= 0) copy_to_clipboard(hwnd, g_vault.entries[vIdx].username);
            return 0;
        }
        case IDM_EDIT_COPY_PASS: {
            int vIdx = get_selected_vault_index();
            if (vIdx >= 0) copy_to_clipboard(hwnd, g_vault.entries[vIdx].password);
            return 0;
        }
        case IDM_EDIT_LAUNCH_URI: {
            int vIdx = get_selected_vault_index();
            if (vIdx >= 0 && !g_vault.entries[vIdx].uri.empty())
                ShellExecuteW(nullptr, L"open", to_wstr(g_vault.entries[vIdx].uri).c_str(), nullptr, nullptr, SW_SHOW);
            return 0;
        }
        case IDM_EDIT_ADD_FOLDER:
            show_folder_dialog(hwnd);
            return 0;
        case IDM_EDIT_RENAME_FOLDER: {
            HTREEITEM hSel = TreeView_GetSelection(g_hTree);
            if (hSel) {
                TVITEM tvi = {};
                tvi.mask = TVIF_PARAM;
                tvi.hItem = hSel;
                TreeView_GetItem(g_hTree, &tvi);
                int idx = (int)tvi.lParam;
                if (idx >= 0 && idx < (int)g_tree_items.size() && g_tree_items[idx].type == TI_FOLDER) {
                    std::string fid = g_tree_items[idx].folderId;
                    for (auto& f : g_vault.folders) {
                        if (f.id == fid) { show_folder_dialog(hwnd, fid, f.name); break; }
                    }
                }
            }
            return 0;
        }
        case IDM_EDIT_DELETE_FOLDER: {
            HTREEITEM hSel = TreeView_GetSelection(g_hTree);
            if (hSel) {
                TVITEM tvi = {};
                tvi.mask = TVIF_PARAM;
                tvi.hItem = hSel;
                TreeView_GetItem(g_hTree, &tvi);
                int idx = (int)tvi.lParam;
                if (idx >= 0 && idx < (int)g_tree_items.size() && g_tree_items[idx].type == TI_FOLDER) {
                    std::string fid = g_tree_items[idx].folderId;
                    for (auto& f : g_vault.folders) {
                        if (f.id == fid) {
                            VBCrypto::delete_folder(fid);
                            VBCrypto::refresh_vault();
                            PostMessage(hwnd, WM_VAULTBOX_REFRESH, 0, 0);
                            vb_log("Folder deleted: " + f.name);
                            break;
                        }
                    }
                }
            }
            return 0;
        }
        case IDM_TOOLS_PASSGEN:
            VBPassGen::show_passgen_dialog(hwnd);
            return 0;
        case IDM_TOOLS_OPEN_DATA:
            ShellExecuteW(nullptr, L"open", g_data_dir.wstring().c_str(), nullptr, nullptr, SW_SHOW);
            return 0;
        case IDM_TOOLS_TOGGLE_LOG:
            g_log_visible = !g_log_visible;
            layout_controls(hwnd);
            return 0;
        case IDM_HELP_ABOUT:
            show_about_dialog(hwnd);
            return 0;
        }
        break;
    }

    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"VaultBox Desktop v0.4.0");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"Show Window");
            AppendMenuW(menu, MF_STRING, IDM_TRAY_QUIT, L"Quit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        if (LOWORD(wp) == IDM_TRAY_SHOW) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (LOWORD(wp) == IDM_TRAY_QUIT) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    // Minimize to tray
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    // Dark theme for child controls
    case WM_CTLCOLORDLG:
        return (LRESULT)g_br_base;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        SetBkMode(hdc, TRANSPARENT);

        // Status bar gets mantle background
        if (id == IDC_STATUSBAR) {
            SetTextColor(hdc, Theme::Subtext0);
            SetBkColor(hdc, Theme::Mantle);
            return (LRESULT)g_br_mantle;
        }
        // Detail panel gets mantle background
        if (id == IDC_DETAIL_PANEL) {
            SetTextColor(hdc, Theme::Text);
            SetBkColor(hdc, Theme::Mantle);
            return (LRESULT)g_br_mantle;
        }
        // Detail panel children
        HWND parent = GetParent((HWND)lp);
        if (parent == g_hDetail) {
            SetTextColor(hdc, Theme::Subtext1);
            SetBkColor(hdc, Theme::Mantle);
            return (LRESULT)g_br_mantle;
        }

        SetTextColor(hdc, Theme::Text);
        SetBkColor(hdc, Theme::Base);
        return (LRESULT)g_br_base;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        SetTextColor(hdc, Theme::Text);

        // Detail panel edit controls get mantle bg
        if (GetParent(ctrl) == g_hDetail) {
            SetBkColor(hdc, Theme::Mantle);
            return (LRESULT)g_br_mantle;
        }
        // Log panel
        if (ctrl == g_hLog) {
            SetTextColor(hdc, Theme::Subtext1);
            SetBkColor(hdc, Theme::Crust);
            return (LRESULT)g_br_crust;
        }

        SetBkColor(hdc, Theme::Surface0);
        return (LRESULT)g_br_surface0;
    }

    case WM_CTLCOLORBTN:
        return (LRESULT)g_br_base;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Fill background
        FillRect(hdc, &rc, g_br_base);

        int w = rc.right, h = rc.bottom;
        int logH = g_log_visible ? LOG_HEIGHT : 0;
        int mainH = h - logH - STATUSBAR_HEIGHT;
        int listH = mainH - DETAIL_HEIGHT - SEPARATOR_WIDTH;

        // Vertical separator between tree and list
        paint_separator(hdc, g_tree_width, 0, 0, mainH, true);

        // Horizontal separator between list and detail
        paint_separator(hdc, g_tree_width + SEPARATOR_WIDTH, listH, w - g_tree_width, 0);

        // Horizontal separator above log (if visible)
        if (g_log_visible) {
            HPEN pen = CreatePen(PS_SOLID, 1, Theme::Surface1);
            HPEN old = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, 0, mainH, nullptr);
            LineTo(hdc, w, mainH);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }

        // Horizontal separator above status bar
        paint_separator(hdc, 0, h - STATUSBAR_HEIGHT, w, 0);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        g_shutdown = true;
        if (g_server) g_server->stop();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_vault.clear();
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
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = main_wndproc;
    wc.hInstance = hInst;
    wc.hbrBackground = g_br_base;
    wc.lpszClassName = L"VaultBoxDesktop";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = create_tray_icon();
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"VaultBoxDesktop",
        L"VaultBox Desktop v0.4.0",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
        nullptr, nullptr, hInst, nullptr);

    return hwnd;
}

} // namespace VBGUI
