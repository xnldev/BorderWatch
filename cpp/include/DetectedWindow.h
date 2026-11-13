#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include "LogScanner.h"
#include "Config.h"

class DetectedWindow {
public:
    DetectedWindow(HINSTANCE hInstance, HWND hParent);
    ~DetectedWindow();

    void Show(const std::vector<DetectedPlayer>& players, const std::wstring& summary, const Config& config, const std::wstring& deepLink = L"");
    void Hide();
    bool IsVisible() const;
    void UpdateData(const std::vector<DetectedPlayer>& players, const std::wstring& summary, const Config& config, const std::wstring& deepLink = L"");
    void ApplyTheme(const Config& config);
    void RelayoutControls(int width, int height);
    void SetScanner(LogScanner* scanner) { m_scanner = scanner; }
    HWND GetHWND() const { return m_hWnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TableWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SearchPillWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ApplyFilter();
    void CopySelectedUsername();
    void OpenSelectedProfile();
    void ShowStatusMessage(const std::wstring& msg);

    HINSTANCE m_hInstance;
    HWND m_hParent;
    HWND m_hWnd = nullptr;
    HWND m_hTopCloseBtn = nullptr;
    HWND m_hTable = nullptr;
    HWND m_hFilterAllBtn = nullptr;
    HWND m_hFilterEnemiesBtn = nullptr;
    HWND m_hFilterAlliesBtn = nullptr;
    HWND m_hSearchContainer = nullptr;
    HWND m_hSearchEdit = nullptr;
    HWND m_hProfileBtn = nullptr;
    HWND m_hCopyBtn = nullptr;
    HWND m_hDeepLinkBtn = nullptr;
    HWND m_hClearBtn = nullptr;
    HWND m_hRefreshBtn = nullptr;
    HWND m_hTooltip = nullptr;
    HFONT m_hFontRegular = nullptr;
    HFONT m_hFontBold = nullptr;

    LogScanner* m_scanner = nullptr;
    bool m_darkMode = false;
    std::wstring m_summaryText;
    std::wstring m_copiedText;

    std::vector<DetectedPlayer> m_allPlayers;
    std::vector<DetectedPlayer> m_filteredPlayers;
    int m_filterMode = 0;
    std::wstring m_searchQuery;
    std::wstring m_serverDeepLink;

    int m_scrollOffset = 0;
    float m_currentScrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    bool m_scrollAnimating = false;
    LONGLONG m_lastScrollQpc = 0;
    int m_hoveredRow = -1;
    int m_selectedRow = -1;
};
