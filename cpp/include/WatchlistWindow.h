#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include "Watchlist.h"
#include "Config.h"

enum class WatchlistSortCol {
    Status = 0,
    Username = 1,
    RobloxId = 2,
    Clan = 3
};

class WatchlistWindow {
public:
    WatchlistWindow(HINSTANCE hInstance, HWND hParent);
    ~WatchlistWindow();

    void Show(const std::vector<PlayerRecord>& players, const Config& config);
    void Hide();
    bool IsVisible() const;
    void UpdateData(const std::vector<PlayerRecord>& players, const Config& config);
    void ApplyTheme(const Config& config);
    void RelayoutControls(int width, int height);
    HWND GetHWND() const { return m_hWnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TableWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SearchPillWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ApplyFilter();
    void SortFilteredPlayers();
    void CopySelectedUsername();
    void CopySelectedId();
    void OpenSelectedProfile();
    void ExportToCsv();
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
    HWND m_hCopyUserBtn = nullptr;
    HWND m_hCopyIdBtn = nullptr;
    HWND m_hExportBtn = nullptr;
    HWND m_hRefreshBtn = nullptr;
    HWND m_hTooltip = nullptr;
    HFONT m_hFontRegular = nullptr;
    HFONT m_hFontBold = nullptr;

    bool m_darkMode = false;
    std::wstring m_statusMsg;

    std::vector<PlayerRecord> m_allPlayers;
    std::vector<PlayerRecord> m_filteredPlayers;
    int m_filterMode = 0; // 0=all, 1=enemies, 2=allies
    std::wstring m_searchQuery;

    WatchlistSortCol m_sortColumn = WatchlistSortCol::Username;
    bool m_sortAscending = true;
    int m_hoveredHeaderCol = -1;

    int m_scrollOffset = 0;
    float m_currentScrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    bool m_scrollAnimating = false;
    int m_hoveredRow = -1;
    int m_selectedRow = -1;
};
