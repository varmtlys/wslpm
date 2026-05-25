#pragma once
#include <windows.h>

namespace Theme {
    constexpr COLORREF BG_DARK      = RGB(240, 240, 245);
    constexpr COLORREF BG_PRIMARY   = RGB(255, 255, 255);
    constexpr COLORREF BG_SECONDARY = RGB(245, 245, 250);
    constexpr COLORREF BG_INPUT     = RGB(250, 250, 252);
    constexpr COLORREF BG_SIDEBAR   = RGB(235, 235, 240);

    constexpr COLORREF ACCENT       = RGB(0, 120, 215);
    constexpr COLORREF ACCENT_DARK  = RGB(0, 90, 158);

    constexpr COLORREF TEXT_PRI     = RGB(32, 32, 32);

    constexpr COLORREF BORDER       = RGB(210, 210, 220);

    constexpr COLORREF BTN_BG      = RGB(225, 225, 235);
    constexpr COLORREF BTN_HOVER   = RGB(210, 210, 220);
    constexpr COLORREF BTN_DANGER  = RGB(231, 76, 60);

    constexpr int WIN_W       = 1020;
    constexpr int WIN_H       = 720;
    constexpr int SIDEBAR_W   = 230;
    constexpr int TOOLBAR_H   = 42;
    constexpr int CTRL_H      = 24;
    constexpr int BTN_H       = 28;
    constexpr int MOUNT_BTN_H = 36;
    constexpr int GAP         = 6;
    constexpr int MARGIN      = 12;
}
