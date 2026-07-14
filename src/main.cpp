/*
 * wslpm — WSL Partition Manager, native C++ Win32 Application
 * Entry point with Common Controls initialization.
 */
#include "app.h"
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Initialize common controls (for ListView, StatusBar, etc.)
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    // Register and create main window
    if (!AppWindow::Register(hInstance)) {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_ICONERROR);
        return 1;
    }
    AppWindow app;
    if (!app.Create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_ICONERROR);
        return 1;
    }

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
