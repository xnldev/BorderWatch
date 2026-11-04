#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>

#define WM_BW_TRAY_MSG (WM_USER + 101)

#define IDM_TRAY_OPEN      3001
#define IDM_TRAY_DETECTED  3002
#define IDM_TRAY_WEB_LINK  3003
#define IDM_TRAY_DEEP_LINK 3004
#define IDM_TRAY_PAUSE     3005
#define IDM_TRAY_QUIT      3006

class TrayIcon {
public:
    TrayIcon(HINSTANCE hInstance, HWND hWnd);
    ~TrayIcon();

    bool Create();
    void Remove();
    void ShowContextMenu();
    void UpdateTooltip(const std::wstring& text);

private:
    HINSTANCE m_hInstance;
    HWND m_hWnd;
    NOTIFYICONDATAW m_nid = { 0 };
    bool m_created = false;
};
