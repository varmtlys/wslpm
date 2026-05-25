#pragma once
#include "operations.h"
#include "theme.h"
#include "resource.h"
#include <windows.h>
#include <atomic>

class AppWindow {
public:
    static bool Register(HINSTANCE hInst);
    bool Create(HINSTANCE hInst);
    HWND hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMsg(UINT msg, WPARAM wp, LPARAM lp);

    void onCreate();
    void onSize(int w, int h);
    void onCommand(WORD id, WORD code, HWND ctl);
    HBRUSH onCtlColor(HDC hdc, HWND ctl, UINT msg);
    void onDrawItem(DRAWITEMSTRUCT* dis);
    void onDestroy();

    // Utilities
    static std::wstring PromptPassword(HWND parent, const std::wstring& title, const std::wstring& text);
    static INT_PTR CALLBACK PasswordDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);

    void initEnv();
    void refreshDisks();
    void refreshDisksUI();
    void onDiskSelected(int idx);
    void onTypeChanged();
    void doMount();
    void doUnmount(int idx);
    void doEject(int idx);
    void doOpen(int idx);
    void unmountAll();
    void updateMountedList();
    void setStatus(const std::wstring& text);
    void showLUKS(bool show);
    void appendLog(const std::wstring& cmd);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInst = nullptr;

    // Controls
    HWND m_editLog = nullptr;
    HWND m_diskLB = nullptr;
    HWND m_comboDisk = nullptr, m_comboPart = nullptr;
    HWND m_radios[8] = {};
    HWND m_lblKey = nullptr, m_editKey = nullptr, m_btnKey = nullptr;
    HWND m_editMP = nullptr, m_btnCreateDir = nullptr, m_comboDistro = nullptr;
    HWND m_chkShortcut = nullptr, m_chkRO = nullptr;
    HWND m_btnMount = nullptr;
    HWND m_lvMounted = nullptr;
    HWND m_btnUnmount = nullptr, m_btnEject = nullptr, m_btnOpen = nullptr;
    HWND m_btnRefresh = nullptr, m_btnUnmountAll = nullptr;
    HWND m_statusBar = nullptr;

    // Theme
    HBRUSH m_brBg = nullptr, m_brInput = nullptr, m_brSidebar = nullptr, m_brDark = nullptr;
    HFONT m_font = nullptr, m_fontBold = nullptr;

    // State
    Operations m_ops;
    std::vector<DiskInfo> m_disks;
    std::vector<std::pair<std::wstring, bool>> m_distros;
    int m_selDisk = -1;
    std::atomic<bool> m_terminating{false};
    std::atomic<int> m_pendingThreads{0};
};
