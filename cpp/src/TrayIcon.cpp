#include "../include/TrayIcon.h"
#include "../include/Resource.h"

TrayIcon::TrayIcon(HINSTANCE hInstance, HWND hWnd)
    : m_hInstance(hInstance), m_hWnd(hWnd) {}

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Create() {
    if (m_created) return true;

    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_BW_TRAY_MSG;
    m_nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!m_nid.hIcon) m_nid.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_INFORMATION);

    wcscpy_s(m_nid.szTip, L"BorderWatch v5.0.2 (Tab + I)");

    m_created = Shell_NotifyIconW(NIM_ADD, &m_nid);
    return m_created;
}

void TrayIcon::Remove() {
    if (m_created) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_created = false;
    }
}

void TrayIcon::UpdateTooltip(const std::wstring& text) {
    if (m_created) {
        wcsncpy_s(m_nid.szTip, text.c_str(), 127);
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}

void TrayIcon::ShowContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TRAY_OPEN, L"Open BorderWatch (Tab + I)");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, IDM_TRAY_DETECTED, L"Detected players");
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, IDM_TRAY_DEEP_LINK, L"Copy Deep Link (roblox://)");
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, IDM_TRAY_PAUSE, L"Pause / Resume");
    InsertMenuW(hMenu, 4, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, 5, MF_BYPOSITION | MF_STRING, IDM_TRAY_QUIT, L"Quit");

    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);
}
