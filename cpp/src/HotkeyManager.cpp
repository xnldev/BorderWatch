#include "../include/HotkeyManager.h"

HHOOK HotkeyManager::s_hHook = nullptr;
HWND HotkeyManager::s_hTargetWnd = nullptr;
bool HotkeyManager::s_tabDown = false;
bool HotkeyManager::s_iDown = false;
bool HotkeyManager::s_chordHeld = false;

bool HotkeyManager::InstallHook(HINSTANCE hInstance, HWND hTargetWnd) {
    if (s_hHook) return true;
    s_hTargetWnd = hTargetWnd;
    s_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    return s_hHook != nullptr;
}

void HotkeyManager::UninstallHook() {
    if (s_hHook) {
        UnhookWindowsHookEx(s_hHook);
        s_hHook = nullptr;
        s_hTargetWnd = nullptr;
    }
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        if (kbd->vkCode == VK_TAB) {
            s_tabDown = isKeyDown;
        } else if (kbd->vkCode == 'I' || kbd->vkCode == 0x49) {
            s_iDown = isKeyDown;
        }

        bool down = s_tabDown && s_iDown;
        if (down && !s_chordHeld) {
            s_chordHeld = true;
            if (s_hTargetWnd && IsWindow(s_hTargetWnd)) {
                PostMessageW(s_hTargetWnd, WM_BW_TOGGLE_PANEL, 0, 0);
            }
        } else if (!down) {
            s_chordHeld = false;
        }
    }
    return CallNextHookEx(s_hHook, nCode, wParam, lParam);
}
