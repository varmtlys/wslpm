#include "app.h"
#include <dwmapi.h>
#include <thread>
#include <mutex>
#include <commctrl.h>
#include <commdlg.h>

static const wchar_t* CLASS_NAME = L"wslpmWindow";

// ── Helpers ──────────────────────────────────────────────

static HWND MakeLabel(HWND parent, HINSTANCE hi, const wchar_t* text,
                       int x, int y, int w, int h, HFONT font, UINT id = 0) {
    HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD|WS_VISIBLE|SS_LEFT,
                               x, y, w, h, parent, (HMENU)(UINT_PTR)id, hi, nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND MakeEdit(HWND parent, HINSTANCE hi, const wchar_t* text,
                      int x, int y, int w, int h, HFONT font, UINT id, DWORD style = 0) {
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                               WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|style,
                               x, y, w, h, parent, (HMENU)(UINT_PTR)id, hi, nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND MakeCombo(HWND parent, HINSTANCE hi,
                       int x, int y, int w, int h, HFONT font, UINT id) {
    HWND hw = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
                               x, y, w, h + 200, parent, (HMENU)(UINT_PTR)id, hi, nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND MakeButton(HWND parent, HINSTANCE hi, const wchar_t* text,
                        int x, int y, int w, int h, HFONT font, UINT id, bool ownerDraw = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | (ownerDraw ? BS_OWNERDRAW : BS_PUSHBUTTON);
    HWND hw = CreateWindowExW(0, L"BUTTON", text, style,
                               x, y, w, h, parent, (HMENU)(UINT_PTR)id, hi, nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

static HWND MakeCheck(HWND parent, HINSTANCE hi, const wchar_t* text,
                       int x, int y, int w, int h, HFONT font, UINT id, bool checked = false) {
    HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
                               x, y, w, h, parent, (HMENU)(UINT_PTR)id, hi, nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    if (checked) SendMessageW(hw, BM_SETCHECK, BST_CHECKED, 0);
    return hw;
}

static HWND MakeRadio(HWND parent, HINSTANCE hi, const wchar_t* text,
                       int x, int y, int w, int h, HFONT font, UINT id, bool first = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
    if (first) style |= WS_GROUP;
    HWND hw = CreateWindowExW(0, L"BUTTON", text, style,
                               x, y, w, h, parent, (HMENU)(UINT_PTR)id, hi, nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

// ── Register & Create ────────────────────────────────────

bool AppWindow::Register(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint ourselves
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&wc) != 0;
}

bool AppWindow::Create(HINSTANCE hInst) {
    m_hInst = hInst;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    m_hwnd = CreateWindowExW(0, CLASS_NAME,
        L"WSL Partition Manager",
        WS_OVERLAPPEDWINDOW,
        (sx - Theme::WIN_W) / 2, (sy - Theme::WIN_H) / 2,
        Theme::WIN_W, Theme::WIN_H,
        nullptr, nullptr, hInst, this);
    if (!m_hwnd) return false;
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

// ── WndProc ──────────────────────────────────────────────

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMsg(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AppWindow::handleMsg(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:     onCreate(); return 0;
    case WM_SIZE:       onSize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_COMMAND:    onCommand(LOWORD(wp), HIWORD(wp), (HWND)lp); return 0;
    case WM_DRAWITEM:   onDrawItem((DRAWITEMSTRUCT*)lp); return TRUE;
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc; GetClientRect(m_hwnd, &rc);
        FillRect(hdc, &rc, m_brBg);
        // Sidebar area
        RECT sb = rc; sb.right = Theme::SIDEBAR_W;
        FillRect(hdc, &sb, m_brSidebar);
        // Toolbar area
        RECT tb = rc; tb.left = 0; tb.bottom = Theme::TOOLBAR_H;
        FillRect(hdc, &tb, m_brDark);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return (LRESULT)onCtlColor((HDC)wp, (HWND)lp, msg);
    case WM_APP_DISKS_LOADED:
        if (m_terminating) return 0;
        refreshDisksUI(); return 0; // m_disks already filled by the background thread
    case WM_APP_DISTROS_LOADED:
        if (m_terminating) return 0;
        SendMessageW(m_comboDistro, CB_RESETCONTENT, 0, 0);
        for (auto& [name, isDef] : m_distros) {
            SendMessageW(m_comboDistro, CB_ADDSTRING, 0, (LPARAM)name.c_str());
            if (isDef) {
                int cnt = (int)SendMessageW(m_comboDistro, CB_GETCOUNT, 0, 0);
                SendMessageW(m_comboDistro, CB_SETCURSEL, cnt - 1, 0);
            }
        }
        if (m_distros.empty())
            SendMessageW(m_comboDistro, CB_ADDSTRING, 0, (LPARAM)L"Not found");
        setStatus(L"WSL distributions loaded");
        return 0;
    case WM_APP_ENV_CHECKED: return 0;
    case WM_APP_MOUNT_DONE: {
        if (m_terminating) return 0;
        auto* data = (std::pair<bool, std::wstring>*)lp;
        setStatus(data->second);
        busy(false);
        EnableWindow(m_btnMount, TRUE);
        SetWindowTextW(m_btnMount, L"\U0001F517  Mount Volume");
        if (data->first) {
            updateMountedList();
        } else if (!data->second.empty()) {
            MessageBoxW(m_hwnd, data->second.c_str(), L"Mount Error", MB_ICONERROR);
        }
        delete data;
        return 0;
    }
    case WM_APP_UNMOUNT_DONE: {
        if (m_terminating) return 0;
        auto* data = (std::pair<bool, std::wstring>*)lp;
        setStatus(data->second);
        busy(false);
        updateMountedList();
        delete data;
        return 0;
    }
    case WM_APP_COMPACT_PROGRESS: {
        if (m_terminating) return 0;
        if (m_progressMarquee) {
            // First percentage arrived — switch marquee to a real progress bar
            SendMessageW(m_progress, PBM_SETMARQUEE, FALSE, 0);
            SetWindowLongPtrW(m_progress, GWL_STYLE,
                GetWindowLongPtrW(m_progress, GWL_STYLE) & ~PBS_MARQUEE);
            m_progressMarquee = false;
        }
        SendMessageW(m_progress, PBM_SETPOS, wp, 0);
        return 0;
    }
    case WM_APP_COMPACT_DONE: {
        if (m_terminating) return 0;
        auto* data = (std::pair<bool, std::wstring>*)lp;
        setStatus(data->second);
        busy(false);
        EnableWindow(m_btnCompact, TRUE);
        SetWindowTextW(m_btnCompact, L"\U0001F5DC Compact VHDX");
        MessageBoxW(m_hwnd, data->second.c_str(), L"Compact VHDX",
                    data->first ? MB_ICONINFORMATION : MB_ICONERROR);
        updateMountedList(); // wsl --shutdown killed any mounts
        delete data;
        return 0;
    }
    case WM_APP_COMMAND_LOG: {
        std::wstring* p = (std::wstring*)lp;
        appendLog(*p);
        delete p;
        return 0;
    }
    case WM_NOTIFY: {
        auto* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_MOUNTED_LV && nm->code == LVN_ITEMCHANGED)
            updateSelButtons();
        break;
    }
    case WM_CLOSE: onDestroy(); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ── Helper to update disk UI ─────────────────────────────
void AppWindow::refreshDisksUI() {
    SendMessageW(m_diskLB, LB_RESETCONTENT, 0, 0);
    SendMessageW(m_comboDisk, CB_RESETCONTENT, 0, 0);
    for (auto& d : m_disks) {
        auto name = d.displayName();
        if (d.isSystem || d.isBoot) name += L" \u26A0";
        SendMessageW(m_diskLB, LB_ADDSTRING, 0, (LPARAM)name.c_str());
        SendMessageW(m_comboDisk, CB_ADDSTRING, 0, (LPARAM)name.c_str());
    }
    if (!m_disks.empty()) {
        SendMessageW(m_comboDisk, CB_SETCURSEL, 0, 0);
        SendMessageW(m_diskLB, LB_SETCURSEL, 0, 0);
        onDiskSelected(0);
    }
    wchar_t buf[64];
    swprintf_s(buf, 64, L"Found %d disk(s)", (int)m_disks.size());
    setStatus(buf);
}

// ── onCreate ─────────────────────────────────────────────

void AppWindow::onCreate() {
    // Dark title bar (disable for light theme)
    BOOL useDarkMode = FALSE;
    DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDarkMode, sizeof(useDarkMode));

    // Brushes
    m_brBg      = CreateSolidBrush(Theme::BG_PRIMARY);
    m_brInput   = CreateSolidBrush(Theme::BG_INPUT);
    m_brSidebar = CreateSolidBrush(Theme::BG_SIDEBAR);
    m_brDark    = CreateSolidBrush(Theme::BG_DARK);

    // Fonts
    LOGFONTW lf{};
    lf.lfHeight = -14; lf.lfWeight = FW_NORMAL; wcscpy_s(lf.lfFaceName, L"Segoe UI");
    m_font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    m_fontBold = CreateFontIndirectW(&lf);

    auto hi = m_hInst;
    int M = Theme::MARGIN, G = Theme::GAP, CH = Theme::CTRL_H;
    int SB = Theme::SIDEBAR_W;
    int TH = Theme::TOOLBAR_H;

    // ── Toolbar ──
    int tx = SB + M;
    m_btnRefresh = MakeButton(m_hwnd, hi, L"\U0001F504 Refresh", tx, 6, 110, 30, m_font, IDC_BTN_REFRESH, true);
    m_btnUnmountAll = MakeButton(m_hwnd, hi, L"\u23CF Unmount All", tx+116, 6, 170, 30, m_font, IDC_BTN_UNMOUNT_ALL, true);
    m_btnCompact = MakeButton(m_hwnd, hi, L"\U0001F5DC Compact VHDX", tx+292, 6, 170, 30, m_font, IDC_BTN_COMPACT, true);

    // Progress bar for long operations (hidden until needed)
    m_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | PBS_MARQUEE | PBS_SMOOTH,
        tx + 468, 10, 220, 22, m_hwnd, (HMENU)(UINT_PTR)IDC_PROGRESS, hi, nullptr);
    SendMessageW(m_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    // ── Sidebar ──
    MakeLabel(m_hwnd, hi, L"\U0001F4BF  Physical Disks", M, TH + M, SB - 2*M, 20, m_fontBold);
    m_diskLB = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,
        M, TH + M + 24, SB - 2*M, 400, m_hwnd, (HMENU)(UINT_PTR)IDC_DISK_LISTBOX, hi, nullptr);
    SendMessageW(m_diskLB, WM_SETFONT, (WPARAM)m_font, TRUE);

    // ── Right panel ──
    int rx = SB + M;
    int ry = TH + M;
    int rw = 0; // computed in onSize

    // Section: Mount
    MakeLabel(m_hwnd, hi, L"\U0001F517  Mount Volume", rx, ry, 400, 22, m_fontBold);
    ry += 26;

    // Row: Disk + Partition combos
    MakeLabel(m_hwnd, hi, L"Disk:", rx, ry, 40, CH, m_font);
    m_comboDisk = MakeCombo(m_hwnd, hi, rx + 44, ry - 2, 300, CH, m_font, IDC_COMBO_DISK);
    MakeLabel(m_hwnd, hi, L"Partition:", rx + 350, ry, 55, CH, m_font);
    m_comboPart = MakeCombo(m_hwnd, hi, rx + 408, ry - 2, 300, CH, m_font, IDC_COMBO_PARTITION);
    ry += CH + G + 4;

    // Row: Volume type radios
    MakeLabel(m_hwnd, hi, L"Volume Type:", rx, ry, 80, CH, m_font);
    ry += CH + 2;
    struct { const wchar_t* t; UINT id; } types[] = {
        {L"\U0001F50D Auto",  IDC_RADIO_AUTO},  {L"\U0001F512 LUKS", IDC_RADIO_LUKS},
        {L"\U0001F4E6 LVM",   IDC_RADIO_LVM},   {L"\U0001F4C1 ext4", IDC_RADIO_EXT4},
        {L"\U0001F4C1 xfs",   IDC_RADIO_XFS},   {L"\U0001F4C1 btrfs",IDC_RADIO_BTRFS},
        {L"\U0001F4C1 ntfs",  IDC_RADIO_NTFS},  {L"\U0001F4C1 vfat", IDC_RADIO_VFAT},
    };
    for (int i = 0; i < 8; i++) {
        int col = i % 4, row = i / 4;
        m_radios[i] = MakeRadio(m_hwnd, hi, types[i].t,
                                 rx + col * 140, ry + row * (CH + 2),
                                 135, CH, m_font, types[i].id, i == 0);
    }
    SendMessageW(m_radios[0], BM_SETCHECK, BST_CHECKED, 0);
    ry += 2 * (CH + 2) + G;

    // Row: LUKS password (REMOVED FROM MAIN WINDOW, NOW MODAL)
    ry += G; 
    m_lblKey = MakeLabel(m_hwnd, hi, L"Or keyfile:", rx, ry, 100, CH, m_font);
    m_editKey = MakeEdit(m_hwnd, hi, L"", rx + 104, ry - 2, 370, CH + 4, m_font, IDC_EDIT_KEYFILE);
    m_btnKey = MakeButton(m_hwnd, hi, L"\U0001F4C2", rx + 480, ry - 2, 36, CH + 4, m_font, IDC_BTN_BROWSE_KEY);
    ry += CH + G + 2;
    showLUKS(false); // hide initially

    // Row: Mount point + Distro
    MakeLabel(m_hwnd, hi, L"Mount point:", rx, ry, 155, CH, m_font);
    m_editMP = MakeEdit(m_hwnd, hi, L"/mnt/data", rx + 158, ry - 2, 170, CH + 4, m_font, IDC_EDIT_MOUNTPOINT);
    m_btnCreateDir = MakeButton(m_hwnd, hi, L"\U0001F4C1", rx + 332, ry - 2, 32, CH + 4, m_font, IDC_BTN_CREATE_DIR);
    MakeLabel(m_hwnd, hi, L"Distribution:", rx + 372, ry, 100, CH, m_font);
    m_comboDistro = MakeCombo(m_hwnd, hi, rx + 472, ry - 2, 202, CH, m_font, IDC_COMBO_DISTRO);
    ry += CH + G + 4;

    // Row: Checkboxes
    m_chkShortcut = MakeCheck(m_hwnd, hi, L"\U0001F4CC Create Explorer Shortcut",
                               rx, ry, 280, CH, m_font, IDC_CHECK_SHORTCUT, true);
    m_chkRO = MakeCheck(m_hwnd, hi, L"\U0001F512 Read Only",
                         rx + 290, ry, 200, CH, m_font, IDC_CHECK_READONLY, true);
    ry += CH + G + 4;

    // Mount button
    m_btnMount = MakeButton(m_hwnd, hi, L"\U0001F517  Mount Volume",
                             rx, ry, 400, Theme::MOUNT_BTN_H, m_fontBold, IDC_BTN_MOUNT, true);
    ry += Theme::MOUNT_BTN_H + M + 4;

    // ── Mounted Volumes Section ──
    MakeLabel(m_hwnd, hi, L"\U0001F4CB  Mounted Volumes", rx, ry, 300, 22, m_fontBold);
    ry += 26;

    m_lvMounted = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOSORTHEADER,
        rx, ry, 700, 140, m_hwnd, (HMENU)(UINT_PTR)IDC_MOUNTED_LV, hi, nullptr);
    SendMessageW(m_lvMounted, WM_SETFONT, (WPARAM)m_font, TRUE);
    ListView_SetExtendedListViewStyle(m_lvMounted, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Theme for ListView (light)
    SetWindowTheme(m_lvMounted, L"Explorer", nullptr);
    ListView_SetBkColor(m_lvMounted, Theme::BG_INPUT);
    ListView_SetTextBkColor(m_lvMounted, Theme::BG_INPUT);
    ListView_SetTextColor(m_lvMounted, Theme::TEXT_PRI);

    // Columns
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (LPWSTR)L"Device"; col.cx = 200; ListView_InsertColumn(m_lvMounted, 0, &col);
    col.pszText = (LPWSTR)L"Type";        col.cx = 80;  ListView_InsertColumn(m_lvMounted, 1, &col);
    col.pszText = (LPWSTR)L"Mount Point"; col.cx = 200; ListView_InsertColumn(m_lvMounted, 2, &col);
    col.pszText = (LPWSTR)L"Distribution"; col.cx = 120; ListView_InsertColumn(m_lvMounted, 3, &col);

    ry += 146;
    m_btnUnmount = MakeButton(m_hwnd, hi, L"\u23CF Unmount", rx, ry, 130, Theme::BTN_H, m_font, IDC_BTN_UNMOUNT_SEL, true);
    m_btnEject   = MakeButton(m_hwnd, hi, L"\U0001F5D1 Eject",   rx+136, ry, 100, Theme::BTN_H, m_font, IDC_BTN_EJECT_SEL, true);
    m_btnOpen    = MakeButton(m_hwnd, hi, L"\U0001F4C2 Open",   rx+242, ry, 100, Theme::BTN_H, m_font, IDC_BTN_OPEN_SEL, true);
    updateSelButtons();
    ry += 40;

    // ── Log Section ──
    MakeLabel(m_hwnd, hi, L"\U0001F50D  Command Log (debug)", rx, ry, 300, 22, m_fontBold);
    ry += 24;
    m_editLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,
        rx, ry, 700, 100, m_hwnd, (HMENU)(UINT_PTR)IDC_EDIT_LOG, hi, nullptr);
    SendMessageW(m_editLog, WM_SETFONT, (WPARAM)m_font, TRUE);
    SendMessageW(m_editLog, EM_SETLIMITTEXT, 0, 0); // Remove limit

    // ── Status bar ──
    m_statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, m_hwnd, (HMENU)(UINT_PTR)IDC_STATUSBAR, hi, nullptr);
    SendMessageW(m_statusBar, SB_SETBKCOLOR, 0, Theme::BG_DARK);

    setStatus(L"Initializing...");

    m_ops.bridge.setLogWindow(m_hwnd);

    // Load data in background
    m_pendingThreads++;
    std::thread([this]() { initEnv(); m_pendingThreads--; }).detach();
}

// ── Dark theming ─────────────────────────────────────────

HBRUSH AppWindow::onCtlColor(HDC hdc, HWND ctl, UINT msg) {
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        SetTextColor(hdc, Theme::TEXT_PRI);
        SetBkMode(hdc, TRANSPARENT);
        // Check if control is in sidebar area
        RECT rc; GetWindowRect(ctl, &rc);
        POINT pt = {rc.left, rc.top}; ScreenToClient(m_hwnd, &pt);
        if (pt.x < Theme::SIDEBAR_W) return m_brSidebar;
        if (pt.y < Theme::TOOLBAR_H) return m_brDark;
        return m_brBg;
    }
    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor(hdc, Theme::TEXT_PRI);
        SetBkColor(hdc, Theme::BG_INPUT);
        return m_brInput;
    }
    if (msg == WM_CTLCOLORLISTBOX) {
        SetTextColor(hdc, Theme::TEXT_PRI);
        SetBkColor(hdc, Theme::BG_INPUT);
        return m_brInput;
    }
    return nullptr;
}

void AppWindow::onDrawItem(DRAWITEMSTRUCT* dis) {
    if (dis->CtlType != ODT_BUTTON) return;

    COLORREF bg, fg;
    bool isMount = (dis->CtlID == IDC_BTN_MOUNT);
    bool isDanger = (dis->CtlID == IDC_BTN_UNMOUNT_ALL || dis->CtlID == IDC_BTN_EJECT_SEL);

    if (isMount) {
        bg = (dis->itemState & ODS_SELECTED) ? Theme::ACCENT_DARK : Theme::ACCENT;
        fg = Theme::BG_DARK;
    } else if (isDanger) {
        bg = (dis->itemState & ODS_SELECTED) ? RGB(150,40,30) : Theme::BTN_DANGER;
        fg = RGB(255, 255, 255);
    } else {
        bg = (dis->itemState & ODS_SELECTED) ? Theme::BTN_HOVER : Theme::BTN_BG;
        fg = Theme::TEXT_PRI;
    }

    HBRUSH br = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::BORDER);
    HBRUSH oldBr = (HBRUSH)SelectObject(dis->hDC, br);
    HPEN oldPen = (HPEN)SelectObject(dis->hDC, pen);
    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right, dis->rcItem.bottom, 6, 6);
    SelectObject(dis->hDC, oldPen);
    SelectObject(dis->hDC, oldBr);
    DeleteObject(pen);
    DeleteObject(br);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    wchar_t text[128];
    GetWindowTextW(dis->hwndItem, text, 128);
    HFONT oldFont = (HFONT)SelectObject(dis->hDC, isMount ? m_fontBold : m_font);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dis->hDC, oldFont);

    if (dis->itemState & ODS_FOCUS) {
        RECT fr = dis->rcItem;
        InflateRect(&fr, -3, -3);
        DrawFocusRect(dis->hDC, &fr);
    }
}

void AppWindow::onSize(int w, int h) {
    SendMessageW(m_statusBar, WM_SIZE, 0, 0);
    
    // Adjust right panel controls width
    int M = Theme::MARGIN;
    int rx = Theme::SIDEBAR_W + M;
    int rw = w - rx - M;

    // Currently most controls are fixed width, but lets at least resize LV and Log
    if (m_lvMounted) SetWindowPos(m_lvMounted, nullptr, 0, 0, rw, 140, SWP_NOMOVE | SWP_NOZORDER);

    // Log window should take remaining space
    RECT statusRect; GetWindowRect(m_statusBar, &statusRect);
    int statusH = statusRect.bottom - statusRect.top;
    
    // Find ry for log
    RECT btnRect; GetWindowRect(m_btnUnmount, &btnRect);
    POINT pt = {btnRect.left, btnRect.bottom}; ScreenToClient(m_hwnd, &pt);
    int logTop = pt.y + 36;
    int logH = h - statusH - logTop - M;
    if (m_editLog && logH > 20) {
        SetWindowPos(m_editLog, nullptr, rx, logTop, rw, logH, SWP_NOZORDER);
    }
}

// ── Event Handlers ───────────────────────────────────────

void AppWindow::onCommand(WORD id, WORD code, HWND ctl) {
    switch (id) {
    case IDC_DISK_LISTBOX:
        if (code == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(m_diskLB, LB_GETCURSEL, 0, 0);
            if (sel >= 0) { SendMessageW(m_comboDisk, CB_SETCURSEL, sel, 0); onDiskSelected(sel); }
        }
        break;
    case IDC_COMBO_DISK:
        if (code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(m_comboDisk, CB_GETCURSEL, 0, 0);
            if (sel >= 0) { SendMessageW(m_diskLB, LB_SETCURSEL, sel, 0); onDiskSelected(sel); }
        }
        break;
    case IDC_BTN_CREATE_DIR: {
        wchar_t path[260]; GetWindowTextW(m_editMP, path, 260);
        int dSel = (int)SendMessageW(m_comboDistro, CB_GETCURSEL, 0, 0);
        if (dSel < 0) break;
        wchar_t distro[100]; SendMessageW(m_comboDistro, CB_GETLBTEXT, dSel, (LPARAM)distro);
        
        if (m_ops.bridge.wslPassword().empty()) {
            std::wstring sp = PromptPassword(m_hwnd, L"Sudo", L"Creating directory requires sudo privileges.\r\nEnter your WSL user password:");
            if (!sp.empty()) {
                m_ops.bridge.setWslPassword(sp);
                if (m_ops.bridge.AuthorizeSudo(distro)) {
                    setStatus(L"Sudo session authorized");
                } else {
                    setStatus(L"Sudo authorization failed");
                }
            } else break;
        }
        
        std::wstring escPath;
        for (wchar_t c : std::wstring(path)) {
            if (c == L'\'') escPath += L"'\\''";
            else escPath += c;
        }
        auto r = m_ops.bridge.runWSLRoot(L"mkdir -p '" + escPath + L"'", distro);
        if (r.success()) {
            MessageBoxW(m_hwnd, L"Directory created successfully!", L"Info", MB_ICONINFORMATION);
        } else {
            std::wstring err = L"Failed to create directory:\n" + r.output + L" " + r.error;
            MessageBoxW(m_hwnd, err.c_str(), L"Error", MB_ICONERROR);
            if (err.find(L"Sorry, try again") != std::wstring::npos || err.find(L"password") != std::wstring::npos) {
                m_ops.bridge.setWslPassword(L""); // clear so we prompt again next time
            }
        }
        break;
    }
    case IDC_RADIO_AUTO: case IDC_RADIO_LUKS: case IDC_RADIO_LVM:
    case IDC_RADIO_EXT4: case IDC_RADIO_XFS:  case IDC_RADIO_BTRFS:
    case IDC_RADIO_NTFS: case IDC_RADIO_VFAT:
        onTypeChanged();
        break;
    case IDC_BTN_BROWSE_KEY: {
        OPENFILENAMEW ofn{};
        wchar_t path[MAX_PATH] = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"All files\0*.*\0Key files\0*.key;*.keyfile\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&ofn))
            SetWindowTextW(m_editKey, path);
        break;
    }
    case IDC_BTN_MOUNT:       doMount(); break;
    case IDC_BTN_REFRESH:     refreshDisks(); break;
    case IDC_BTN_UNMOUNT_ALL: unmountAll(); break;
    case IDC_BTN_COMPACT:     doCompact(); break;
    case IDC_BTN_UNMOUNT_SEL: {
        int sel = ListView_GetNextItem(m_lvMounted, -1, LVNI_SELECTED);
        if (sel >= 0) doUnmount(sel);
        break;
    }
    case IDC_BTN_EJECT_SEL: {
        int sel = ListView_GetNextItem(m_lvMounted, -1, LVNI_SELECTED);
        if (sel >= 0) doEject(sel);
        break;
    }
    case IDC_BTN_OPEN_SEL: {
        int sel = ListView_GetNextItem(m_lvMounted, -1, LVNI_SELECTED);
        if (sel >= 0) doOpen(sel);
        break;
    }
    }
}

// ── Actions ──────────────────────────────────────────────

void AppWindow::showLUKS(bool show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(m_lblKey, cmd); ShowWindow(m_editKey, cmd); ShowWindow(m_btnKey, cmd);
}

void AppWindow::onTypeChanged() {
    bool isLuks = (SendMessageW(m_radios[1], BM_GETCHECK, 0, 0) == BST_CHECKED);
    showLUKS(isLuks);
}

void AppWindow::onDiskSelected(int idx) {
    if (idx < 0 || idx >= (int)m_disks.size()) return;
    m_selDisk = idx;
    auto& disk = m_disks[idx];

    SendMessageW(m_comboPart, CB_RESETCONTENT, 0, 0);
    SendMessageW(m_comboPart, CB_ADDSTRING, 0, (LPARAM)L"Entire disk (bare)");
    for (auto& p : disk.partitions) {
        wchar_t buf[128];
        swprintf_s(buf, L"Partition %d \u2014 %s [%s]", p.number, p.sizeDisplay().c_str(), p.typeId.c_str());
        SendMessageW(m_comboPart, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessageW(m_comboPart, CB_SETCURSEL, disk.partitions.empty() ? 0 : 1, 0);

    wchar_t mp[64]; swprintf_s(mp, L"/mnt/disk%d", disk.number);
    SetWindowTextW(m_editMP, mp);
}

void AppWindow::initEnv() {
    std::wstring wslMsg;
    bool wslOk = m_ops.bridge.checkWSLInstalled(wslMsg);
    if (!wslOk) {
        PostMessageW(m_hwnd, WM_APP_ENV_CHECKED, 0, 0);
        return;
    }

    m_distros = m_ops.bridge.getDistros();
    PostMessageW(m_hwnd, WM_APP_DISTROS_LOADED, 0, 0);

    m_disks = m_ops.getPhysicalDisks();
    PostMessageW(m_hwnd, WM_APP_DISKS_LOADED, 0, 0);
}

void AppWindow::refreshDisks() {
    setStatus(L"Refreshing disks...");
    m_pendingThreads++;
    std::thread([this]() {
        m_disks = m_ops.getPhysicalDisks();
        if (!m_terminating) PostMessageW(m_hwnd, WM_APP_DISKS_LOADED, 0, 0);
        m_pendingThreads--;
    }).detach();
}

void AppWindow::busy(bool on) {
    if (on) {
        m_progressMarquee = true;
        SetWindowLongPtrW(m_progress, GWL_STYLE,
            GetWindowLongPtrW(m_progress, GWL_STYLE) | PBS_MARQUEE);
        SendMessageW(m_progress, PBM_SETMARQUEE, TRUE, 0);
        ShowWindow(m_progress, SW_SHOW);
    } else {
        m_progressMarquee = false;
        SendMessageW(m_progress, PBM_SETMARQUEE, FALSE, 0);
        ShowWindow(m_progress, SW_HIDE);
        SendMessageW(m_progress, PBM_SETPOS, 0, 0);
    }
}

void AppWindow::setStatus(const std::wstring& text) {
    SendMessageW(m_statusBar, SB_SETTEXTW, 0, (LPARAM)text.c_str());
    appendLog(L"STATUS: " + text + L"\r\n");
}

void AppWindow::appendLog(const std::wstring& cmd) {
    if (!m_editLog) return;
    int len = GetWindowTextLengthW(m_editLog);
    SendMessageW(m_editLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(m_editLog, EM_REPLACESEL, 0, (LPARAM)cmd.c_str());
    SendMessageW(m_editLog, WM_VSCROLL, SB_BOTTOM, 0);
}

void AppWindow::doMount() {
    if (m_selDisk < 0 || m_selDisk >= (int)m_disks.size()) {
        MessageBoxW(m_hwnd, L"Select a disk", L"Warning", MB_ICONWARNING); return;
    }

    auto& disk = m_disks[m_selDisk];
    if (disk.isSystem || disk.isBoot) {
        MessageBoxW(m_hwnd,
            L"This is a system/boot disk. Attaching it to WSL would take it away\n"
            L"from Windows and can crash the system. Mounting is blocked.",
            L"Blocked", MB_ICONERROR);
        return;
    }
    int partSel = (int)SendMessageW(m_comboPart, CB_GETCURSEL, 0, 0);
    int partNum = (partSel <= 0) ? 0 : disk.partitions[partSel - 1].number;

    std::wstring volType = L"auto";
    const wchar_t* typeNames[] = {L"auto", L"luks", L"lvm", L"ext4", L"xfs", L"btrfs", L"ntfs3", L"vfat"};
    for (int i = 0; i < 8; i++) {
        if (SendMessageW(m_radios[i], BM_GETCHECK, 0, 0) == BST_CHECKED) { volType = typeNames[i]; break; }
    }

    wchar_t mpBuf[260]; GetWindowTextW(m_editMP, mpBuf, 260);
    std::wstring mountPoint = mpBuf;
    while (!mountPoint.empty() && mountPoint.back() == L' ') mountPoint.pop_back();
    while (!mountPoint.empty() && mountPoint.front() == L' ') mountPoint.erase(mountPoint.begin());
    if (mountPoint.size() < 2 || mountPoint[0] != L'/' ||
        mountPoint.find(L"..") != std::wstring::npos) {
        MessageBoxW(m_hwnd, L"Enter a valid mount point (absolute path like /mnt/data, no \"..\")",
                     L"Error", MB_ICONERROR); return;
    }
    {
        std::lock_guard<std::mutex> lk(m_ops.mtx());
        for (auto& v : m_ops.mounted()) {
            if (v.mountPoint == mountPoint) {
                MessageBoxW(m_hwnd, (L"Mount point " + mountPoint + L" is already in use by " +
                            v.device).c_str(), L"Error", MB_ICONERROR);
                return;
            }
        }
    }

    wchar_t distroBuf[128]; int dSel = (int)SendMessageW(m_comboDistro, CB_GETCURSEL, 0, 0);
    if (dSel < 0) { MessageBoxW(m_hwnd, L"Select a WSL distribution", L"Error", MB_ICONERROR); return; }
    SendMessageW(m_comboDistro, CB_GETLBTEXT, dSel, (LPARAM)distroBuf);
    std::wstring distro = distroBuf;

    bool createShortcut = (SendMessageW(m_chkShortcut, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool readOnly = (SendMessageW(m_chkRO, BM_GETCHECK, 0, 0) == BST_CHECKED);

    // LUKS password/key — collect on UI thread for both explicit LUKS and auto-detect
    std::string password;
    std::wstring keyfile;
    if (volType == L"luks" || volType == L"auto") {
        wchar_t keyBuf[260]; GetWindowTextW(m_editKey, keyBuf, 260);
        keyfile = keyBuf;
        if (keyfile.empty()) {
            std::wstring pw = PromptPassword(m_hwnd, L"\U0001F512 LUKS Password",
                volType == L"luks" ? L"Enter password to unlock LUKS container:"
                                   : L"Enter LUKS password (if needed by auto-detection; empty to skip):");
            if (!pw.empty()) password = WSLBridge::WideToUTF8(pw);
            if (volType == L"luks" && password.empty()) return;
        }
    }

    // Sudo password — collect on UI thread; cancelling aborts the mount
    if (m_ops.bridge.wslPassword().empty()) {
        std::wstring sp = PromptPassword(m_hwnd, L"Sudo",
            L"WSL commands require sudo privileges.\r\nEnter your WSL user password:");
        if (sp.empty()) { setStatus(L"Mount cancelled"); return; }
        m_ops.bridge.setWslPassword(sp);
    }

    EnableWindow(m_btnMount, FALSE);
    SetWindowTextW(m_btnMount, L"\u23F3  Mounting...");
    setStatus(L"Mounting...");
    busy(true);

    int diskNum = disk.number;
    m_pendingThreads++;
    std::thread([=]() {
        std::wstring msg;
        bool ok = false;
        std::string currentPwd = password;

        // Step 1: Attach disk
        bool bare = (volType == L"luks" || volType == L"lvm");
        if (bare || partNum == 0) {
            if (!m_ops.attachDisk(diskNum, bare, msg)) {
                m_pendingThreads--;
                PostMessageW(m_hwnd, WM_APP_MOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(false, msg));
                return;
            }
            // Poll for device to appear instead of blind Sleep
            for (int i = 0; i < 15; i++) {
                Sleep(200);
                auto probe = m_ops.bridge.runWSL(L"lsblk -n -o NAME 2>/dev/null | grep -q sd && echo OK", distro);
                if (probe.success() && probe.output.find(L"OK") != std::wstring::npos) break;
            }
        } else if (!bare && partNum > 0) {
            std::wstring fs = (volType != L"auto" && volType != L"luks" && volType != L"lvm") ? volType : L"";
            if (!m_ops.attachPartition(diskNum, partNum, fs, msg)) {
                m_pendingThreads--;
                PostMessageW(m_hwnd, WM_APP_MOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(false, msg));
                return;
            }
            uint64_t partSize = (partSel > 0 && partSel - 1 < (int)disk.partitions.size())
                ? disk.partitions[partSel - 1].sizeBytes : disk.sizeBytes;
            auto dev = m_ops.findWSLDevice(diskNum, partNum, disk.sizeBytes, partSize, distro);
            if (dev.empty()) {
                std::wstring dummy;
                m_ops.detachDisk(diskNum, dummy);
                m_pendingThreads--;
                PostMessageW(m_hwnd, WM_APP_MOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(false,
                    L"Could not find the attached device inside WSL (no size match in lsblk)."));
                return;
            }
            ok = m_ops.mountPlain(dev, mountPoint, fs, distro, readOnly, diskNum, partNum, msg);
            if (!ok && msg == L"SUDO_PASSWORD_REQUIRED")
                msg = L"Sudo authorization required. Enter password and try again.";
            PostMessageW(m_hwnd, WM_APP_MOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(ok, msg));
            m_pendingThreads--;
            return;
        }

        // Step 2: Find device in WSL
        uint64_t expectedSize = disk.sizeBytes;
        if (partNum > 0 && partSel > 0 && partSel - 1 < (int)disk.partitions.size()) {
            expectedSize = disk.partitions[partSel - 1].sizeBytes;
        }
        auto device = m_ops.findWSLDevice(diskNum, partNum, disk.sizeBytes, expectedSize, distro);
        if (device.empty()) {
            msg = L"Could not find the attached device inside WSL (no size match in lsblk).";
        } else {
            // Step 3: Auto-detect
            std::wstring actualType = volType;
            if (actualType == L"auto") {
                actualType = m_ops.detectVolumeType(device, distro);
            }

            // Step 4: Mount (single attempt — all passwords collected on UI thread)
            if (actualType == L"luks") {
                if (currentPwd.empty() && keyfile.empty()) {
                    msg = L"Auto-detection found LUKS but no password provided. Select LUKS type explicitly and enter password.";
                } else {
                    ok = m_ops.mountLUKS(device, mountPoint, currentPwd, keyfile, L"", distro, readOnly, diskNum, partNum, msg);
                }
            } else if (actualType == L"lvm") {
                ok = m_ops.mountLVM(device, mountPoint, distro, readOnly, diskNum, partNum, msg);
            } else {
                std::wstring fs = (actualType != L"auto") ? actualType : L"";
                ok = m_ops.mountPlain(device, mountPoint, fs, distro, readOnly, diskNum, partNum, msg);
            }
        }

        if (!ok && msg == L"SUDO_PASSWORD_REQUIRED") {
            msg = L"Sudo authorization required. Enter password and try again.";
        }

        if (ok && createShortcut) {
            std::wstring sm;
            m_ops.createShortcut(distro, mountPoint, L"", sm);
        }

        if (!ok) {
            std::wstring dummy;
            m_ops.detachDisk(diskNum, dummy);
        }
        PostMessageW(m_hwnd, WM_APP_MOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(ok, msg));
        m_pendingThreads--;
    }).detach();
}

void AppWindow::doUnmount(int idx) {
    // Snapshot the mount point at click time — the list may change before the thread runs
    std::wstring mp;
    {
        std::lock_guard<std::mutex> lk(m_ops.mtx());
        if (idx < 0 || idx >= (int)m_ops.mounted().size()) return;
        mp = m_ops.mounted()[idx].mountPoint;
    }
    if (MessageBoxW(m_hwnd, L"Unmount selected volume?", L"Confirm",
                     MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    busy(true);
    m_pendingThreads++;
    std::thread([=]() {
        std::wstring msg;
        m_ops.unmountByMountPoint(mp, msg);
        if (!m_terminating) PostMessageW(m_hwnd, WM_APP_UNMOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(true, msg));
        m_pendingThreads--;
    }).detach();
}

void AppWindow::doEject(int idx) {
    int diskNum = -1;
    {
        std::lock_guard<std::mutex> lk(m_ops.mtx());
        if (idx < 0 || idx >= (int)m_ops.mounted().size()) return;
        diskNum = m_ops.mounted()[idx].diskNumber;
    }
    if (MessageBoxW(m_hwnd, L"Unmount and safely eject disk?", L"Eject",
                     MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    busy(true);
    m_pendingThreads++;
    std::thread([=]() {
        std::wstring msg;
        std::vector<std::wstring> toUnmount;
        {
            std::lock_guard<std::mutex> lk(m_ops.mtx());
            for (auto& v : m_ops.mounted()) {
                if (v.diskNumber == diskNum) toUnmount.push_back(v.mountPoint);
            }
        }
        for (auto& mp : toUnmount) m_ops.unmountByMountPoint(mp, msg);
        m_ops.safeEject(diskNum, msg);
        if (!m_terminating) PostMessageW(m_hwnd, WM_APP_UNMOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(true, msg));
        m_pendingThreads--;
    }).detach();
}

void AppWindow::doOpen(int idx) {
    MountedVolume v;
    {
        std::lock_guard<std::mutex> lk(m_ops.mtx());
        if (idx < 0 || idx >= (int)m_ops.mounted().size()) return;
        v = m_ops.mounted()[idx];
    }
    m_ops.openInExplorer(v.distro.empty() ? L"Ubuntu" : v.distro, v.mountPoint);
}

struct CompactDlgParam {
    std::vector<WSLImage>* images;
    int selected = -1;
    bool zeroFree = false;
};

INT_PTR CALLBACK AppWindow::CompactDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* p = (CompactDlgParam*)GetWindowLongPtrW(hDlg, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)lp);
        p = (CompactDlgParam*)lp;
        HWND lb = GetDlgItem(hDlg, IDC_LIST_IMAGES);
        for (auto& img : *p->images) {
            std::wstring s = img.name + L" — " + img.sizeDisplay() + L"  (" + img.vhdxPath + L")";
            SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)s.c_str());
        }
        SendMessageW(lb, LB_SETCURSEL, 0, 0);
        return TRUE;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == IDOK || (id == IDC_LIST_IMAGES && HIWORD(wp) == LBN_DBLCLK)) {
            int sel = (int)SendMessageW(GetDlgItem(hDlg, IDC_LIST_IMAGES), LB_GETCURSEL, 0, 0);
            if (sel < 0) return TRUE; // nothing selected
            p->selected = sel;
            p->zeroFree = IsDlgButtonChecked(hDlg, IDC_CHECK_ZEROFREE) == BST_CHECKED;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    }
    }
    return FALSE;
}

void AppWindow::doCompact() {
    {
        std::lock_guard<std::mutex> lk(m_ops.mtx());
        if (!m_ops.mounted().empty()) {
            MessageBoxW(m_hwnd, L"Unmount all volumes first — compaction shuts down WSL entirely.",
                        L"Compact VHDX", MB_ICONWARNING);
            return;
        }
    }

    // Enumerate all registered WSL images and let the user pick one
    setStatus(L"Scanning WSL images...");
    auto images = m_ops.getWSLImages();
    if (images.empty()) {
        setStatus(L"No WSL images found");
        MessageBoxW(m_hwnd, L"No WSL distribution images (ext4.vhdx) found in the registry.",
                    L"Compact VHDX", MB_ICONWARNING);
        return;
    }

    CompactDlgParam param{ &images };
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DIALOG_COMPACT),
                        m_hwnd, CompactDlgProc, (LPARAM)&param) != IDOK || param.selected < 0) {
        setStatus(L"Compaction cancelled");
        return;
    }
    WSLImage img = images[param.selected];
    bool zeroFree = param.zeroFree;

    // Zero-fill runs as root inside the distro
    if (zeroFree && m_ops.bridge.wslPassword().empty()) {
        std::wstring sp = PromptPassword(m_hwnd, L"Sudo",
            L"Zeroing free space requires sudo.\r\nEnter your WSL user password:");
        if (!sp.empty()) m_ops.bridge.setWslPassword(sp);
        else zeroFree = false;
    }

    EnableWindow(m_btnCompact, FALSE);
    SetWindowTextW(m_btnCompact, L"⏳ Compacting...");
    setStatus(L"Compacting '" + img.name + L"' (" + img.sizeDisplay() + L")...");

    busy(true); // marquee until diskpart starts reporting percentages

    HWND hwnd = m_hwnd;
    m_ops.bridge.setProgressCallback([hwnd](int p) {
        PostMessageW(hwnd, WM_APP_COMPACT_PROGRESS, (WPARAM)p, 0);
    });

    m_pendingThreads++;
    std::thread([=]() {
        std::wstring msg;
        bool ok = m_ops.compactDistro(img.name, img.vhdxPath, zeroFree, msg);
        m_ops.bridge.setProgressCallback(nullptr);
        if (!m_terminating)
            PostMessageW(m_hwnd, WM_APP_COMPACT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(ok, msg));
        m_pendingThreads--;
    }).detach();
}

void AppWindow::unmountAll() {
    {
        std::lock_guard<std::mutex> lk(m_ops.mtx());
        if (m_ops.mounted().empty()) { setStatus(L"No mounted volumes"); return; }
    }
    if (MessageBoxW(m_hwnd, L"Unmount all volumes?", L"Confirm",
                     MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    busy(true);
    m_pendingThreads++;
    std::thread([this]() {
        std::wstring msg;
        m_ops.unmountAll(msg);
        m_ops.cleanupShortcuts();
        if (!m_terminating) PostMessageW(m_hwnd, WM_APP_UNMOUNT_DONE, 0, (LPARAM)new std::pair<bool, std::wstring>(true, msg));
        m_pendingThreads--;
    }).detach();
}

void AppWindow::updateSelButtons() {
    BOOL has = ListView_GetNextItem(m_lvMounted, -1, LVNI_SELECTED) >= 0;
    EnableWindow(m_btnUnmount, has);
    EnableWindow(m_btnEject, has);
    EnableWindow(m_btnOpen, has);
}

void AppWindow::updateMountedList() {
    ListView_DeleteAllItems(m_lvMounted);
    std::lock_guard<std::mutex> lk(m_ops.mtx());
    auto& vols = m_ops.mounted();
    for (int i = 0; i < (int)vols.size(); i++) {
        auto& v = vols[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = (LPWSTR)v.displayDevice().c_str();
        ListView_InsertItem(m_lvMounted, &item);
        ListView_SetItemText(m_lvMounted, i, 1, (LPWSTR)v.volumeType.c_str());
        ListView_SetItemText(m_lvMounted, i, 2, (LPWSTR)v.mountPoint.c_str());
        ListView_SetItemText(m_lvMounted, i, 3, (LPWSTR)v.distro.c_str());
    }
}

struct PwdDlgParam { std::wstring title; std::wstring prompt; std::wstring result; };

std::wstring AppWindow::PromptPassword(HWND parent, const std::wstring& title, const std::wstring& text) {
    PwdDlgParam param = { title, text, L"" };
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DIALOG_PASSWORD), parent, PasswordDlgProc, (LPARAM)&param) == IDOK) {
        return param.result;
    }
    return L"";
}

INT_PTR CALLBACK AppWindow::PasswordDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* p = (PwdDlgParam*)GetWindowLongPtrW(hDlg, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)lp);
        p = (PwdDlgParam*)lp;
        SetWindowTextW(hDlg, p->title.c_str());
        SetDlgItemTextW(hDlg, IDC_STATIC_TEXT, p->prompt.c_str());
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[256];
            GetDlgItemTextW(hDlg, IDC_EDIT_PWD_INPUT, buf, 256);
            p->result = buf;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void AppWindow::onDestroy() {
    if (m_terminating) return; // re-entrant WM_CLOSE while pumping messages below

    // A background operation (mount/unmount/compact) is running — confirm twice
    if (m_pendingThreads > 0) {
        if (MessageBoxW(m_hwnd,
                L"A background operation is still running (mount, unmount or VHDX compaction).\n\n"
                L"Closing now will wait for it to finish. Close anyway?",
                L"Operation in progress", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
        if (MessageBoxW(m_hwnd,
                L"Are you sure? Interrupting a compaction or mount operation\n"
                L"may leave WSL or the disk image in an inconsistent state.",
                L"Confirm close", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }

    m_terminating = true;
    // Wait for background threads to finish
    while (m_pendingThreads > 0) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }
    bool hasMounted = false;
    {
        std::lock_guard<std::mutex> lk(m_ops.mtx());
        hasMounted = !m_ops.mounted().empty();
    }
    if (hasMounted) {
        int r = MessageBoxW(m_hwnd, L"Mounted volumes exist.\n\nYes \u2014 unmount and exit\nNo \u2014 exit without unmounting",
                             L"Close", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) { m_terminating = false; return; }
        if (r == IDYES) {
            std::wstring msg;
            m_ops.unmountAll(msg);
            m_ops.cleanupShortcuts();
        }
    }
    DeleteObject(m_brBg); DeleteObject(m_brInput);
    DeleteObject(m_brSidebar);
    DeleteObject(m_brDark);
    DeleteObject(m_font); DeleteObject(m_fontBold);
    DestroyWindow(m_hwnd);
}
