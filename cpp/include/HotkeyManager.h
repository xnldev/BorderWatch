#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define WM_BW_TOGGLE_PANEL (WM_USER + 100)

class HotkeyManager {
public:
    static bool InstallHook(HINSTANCE hInstance, HWND hTargetWnd);
    static void UninstallHook();

private:
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    static HHOOK s_hHook;
    static HWND s_hTargetWnd;
    static bool s_tabDown;
    static bool s_iDown;
    static bool s_chordHeld;
};
