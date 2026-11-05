#include "../include/WatchlistWindow.h"
#include "../include/Utils.h"
#include <algorithm>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

static const wchar_t* WL_WINDOW_CLASS_NAME = L"BorderWatchWatchlistClass";
static const wchar_t* WL_TABLE_CLASS_NAME  = L"BorderWatchWatchlistTable";
static const wchar_t* WL_PILL_CLASS_NAME   = L"BorderWatchWatchlistPill";

enum WatchlistControlId {
    IDC_WL_REFRESH = 401,
    IDC_WL_FILTER_ALL = 402,
    IDC_WL_FILTER_ENEMIES = 403,
    IDC_WL_FILTER_ALLIES = 404,
    IDC_WL_EDT_SEARCH = 405,
    IDC_WL_BTN_PROFILE = 406,
    IDC_WL_BTN_COPY_USER = 407,
    IDC_WL_BTN_COPY_ID = 408,
    IDC_WL_BTN_EXPORT = 409,
    IDC_WL_BTN_TOP_CLOSE = 410
};

enum WatchlistContextId {
    IDM_WL_CTX_OPEN_PROFILE = 5001,
    IDM_WL_CTX_COPY_PROFILE_URL = 5002,
    IDM_WL_CTX_COPY_USERNAME = 5003,
    IDM_WL_CTX_COPY_USER_ID = 5004,
    IDM_WL_CTX_COPY_FULL_INFO = 5005
};

static void OpenRobloxProfileUrl(const std::wstring& username, uint64_t userId = 0) {
    if (userId > 0) {
        std::wstring url = L"https://www.roblox.com/users/" + std::to_wstring(userId) + L"/profile";
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (!username.empty()) {
        std::wstring url = L"https://www.roblox.com/search/users?keyword=" + username;
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

static WNDPROC s_oldWlBtnProc = nullptr;
static LRESULT CALLBACK WlHandCursorBtnSubclass(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        if (!GetPropW(hWnd, L"Hover")) {
            SetPropW(hWnd, L"Hover", (HANDLE)1);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE:
        RemovePropW(hWnd, L"Hover");
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    return CallWindowProcW(s_oldWlBtnProc, hWnd, msg, wParam, lParam);
}

WatchlistWindow::WatchlistWindow(HINSTANCE hInstance, HWND hParent)
    : m_hInstance(hInstance), m_hParent(hParent) {}

WatchlistWindow::~WatchlistWindow() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
    }
    if (m_hFontRegular) DeleteObject(m_hFontRegular);
    if (m_hFontBold) DeleteObject(m_hFontBold);
}

void WatchlistWindow::ShowStatusMessage(const std::wstring& msg) {
    m_statusMsg = msg;
    if (m_hWnd && IsWindow(m_hWnd)) {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        RECT rcFooter = { 18, rc.bottom - 44, rc.right - 18, rc.bottom };
        InvalidateRect(m_hWnd, &rcFooter, FALSE);
        SetTimer(m_hWnd, 998, 3500, nullptr);
    }
}

void WatchlistWindow::SortFilteredPlayers() {
    auto cmp = [this](const PlayerRecord& a, const PlayerRecord& b) -> bool {
        int diff = 0;
        switch (m_sortColumn) {
        case WatchlistSortCol::Status:
            diff = a.status.compare(b.status);
            if (diff == 0) diff = _wcsicmp(a.username.c_str(), b.username.c_str());
            break;
        case WatchlistSortCol::Username:
            diff = _wcsicmp(a.username.c_str(), b.username.c_str());
            break;
        case WatchlistSortCol::RobloxId:
            if (a.roblox_id != b.roblox_id) {
                return m_sortAscending ? (a.roblox_id < b.roblox_id) : (a.roblox_id > b.roblox_id);
            }
            diff = _wcsicmp(a.username.c_str(), b.username.c_str());
            break;
        case WatchlistSortCol::Clan:
            diff = _wcsicmp(a.clan.c_str(), b.clan.c_str());
            if (diff == 0) diff = _wcsicmp(a.username.c_str(), b.username.c_str());
            break;
        }
        return m_sortAscending ? (diff < 0) : (diff > 0);
    };

    std::sort(m_filteredPlayers.begin(), m_filteredPlayers.end(), cmp);
}

void WatchlistWindow::ApplyFilter() {
    std::wstring q = m_searchQuery;
    while (!q.empty() && (q.front() == L' ' || q.front() == L'\t')) q.erase(q.begin());
    while (!q.empty() && (q.back() == L' ' || q.back() == L'\t')) q.pop_back();
    std::transform(q.begin(), q.end(), q.begin(), ::towlower);

    m_filteredPlayers.clear();
    m_filteredPlayers.reserve(m_allPlayers.size());

    for (const auto& p : m_allPlayers) {
        if (m_filterMode == 1 && p.status != L"Enemy") continue;
        if (m_filterMode == 2 && p.status != L"Ally") continue;

        if (!q.empty()) {
            std::wstring u = p.username;
            std::transform(u.begin(), u.end(), u.begin(), ::towlower);

            std::wstring c = p.clan;
            std::transform(c.begin(), c.end(), c.begin(), ::towlower);

            std::wstring s = p.status;
            std::transform(s.begin(), s.end(), s.begin(), ::towlower);

            std::wstring idStr = std::to_wstring(p.roblox_id);

            if (u.find(q) == std::wstring::npos &&
                c.find(q) == std::wstring::npos &&
                s.find(q) == std::wstring::npos &&
                idStr.find(q) == std::wstring::npos) {
                continue;
            }
        }
        m_filteredPlayers.push_back(p);
    }

    SortFilteredPlayers();

    m_currentScrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_scrollOffset = 0;
    m_hoveredRow = -1;
    m_selectedRow = -1;

    if (m_hTable && IsWindow(m_hTable)) {
        InvalidateRect(m_hTable, nullptr, FALSE);
    }
    if (m_hWnd && IsWindow(m_hWnd)) {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void WatchlistWindow::CopySelectedUsername() {
    if (m_selectedRow >= 0 && m_selectedRow < (int)m_filteredPlayers.size()) {
        const auto& p = m_filteredPlayers[m_selectedRow];
        Utils::CopyTextToClipboard(m_hWnd, p.username);
        ShowStatusMessage(L"\u2713 Copied '" + p.username + L"' to clipboard!");
    } else {
        ShowStatusMessage(L"Select a player first.");
    }
}

void WatchlistWindow::CopySelectedId() {
    if (m_selectedRow >= 0 && m_selectedRow < (int)m_filteredPlayers.size()) {
        const auto& p = m_filteredPlayers[m_selectedRow];
        if (p.roblox_id > 0) {
            Utils::CopyTextToClipboard(m_hWnd, std::to_wstring(p.roblox_id));
            ShowStatusMessage(L"\u2713 Copied Roblox ID " + std::to_wstring(p.roblox_id) + L"!");
        } else {
            ShowStatusMessage(L"No Roblox ID available for this player.");
        }
    } else {
        ShowStatusMessage(L"Select a player first.");
    }
}

void WatchlistWindow::OpenSelectedProfile() {
    if (m_selectedRow >= 0 && m_selectedRow < (int)m_filteredPlayers.size()) {
        const auto& p = m_filteredPlayers[m_selectedRow];
        OpenRobloxProfileUrl(p.username, p.roblox_id);
        ShowStatusMessage(L"Opening @" + p.username + L" profile...");
    } else {
        ShowStatusMessage(L"Select a player first.");
    }
}

void WatchlistWindow::ExportToCsv() {
    if (m_filteredPlayers.empty()) {
        ShowStatusMessage(L"No players to export.");
        return;
    }

    wchar_t szPath[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, szPath))) {
        ShowStatusMessage(L"Failed to locate Desktop directory.");
        return;
    }

    std::wstring outPath = Utils::CombinePath(szPath, L"BorderWatch-Watchlist.csv");
    std::ofstream ofs(outPath, std::ios::out | std::ios::binary);
    if (!ofs.is_open()) {
        ShowStatusMessage(L"Failed to create CSV file on Desktop.");
        return;
    }

    // UTF-8 BOM
    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    ofs.write((char*)bom, 3);

    // CSV Header
    ofs << "Status,Username,RobloxId,Clan\r\n";

    for (const auto& p : m_filteredPlayers) {
        std::string status = Utils::WideToUtf8(p.status);
        std::string user = Utils::WideToUtf8(p.username);
        std::string clan = Utils::WideToUtf8(p.clan);

        ofs << "\"" << status << "\",\"" << user << "\","
            << p.roblox_id << ",\"" << clan << "\"\r\n";
    }
    ofs.close();

    ShowStatusMessage(L"\u2713 Exported " + std::to_wstring(m_filteredPlayers.size()) + L" players to Desktop (BorderWatch-Watchlist.csv)");
}

LRESULT CALLBACK WatchlistWindow::SearchPillWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WatchlistWindow* self = reinterpret_cast<WatchlistWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
        return TRUE;
    case WM_LBUTTONDOWN:
        if (self && self->m_hSearchEdit) {
            SetFocus(self->m_hSearchEdit);
        }
        return 0;
    case WM_COMMAND: {
        HWND hParent = GetParent(hWnd);
        if (hParent) {
            SendMessageW(hParent, WM_COMMAND, wParam, lParam);
        }
        if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS) {
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        bool isDark = (self && self->m_darkMode);
        COLORREF bg = isDark ? RGB(30, 35, 48) : RGB(255, 255, 255);
        COLORREF text = isDark ? RGB(241, 243, 250) : RGB(36, 41, 56);
        SetBkColor(hdc, bg);
        SetTextColor(hdc, text);
        static HBRUSH hDarkPillBg = CreateSolidBrush(RGB(30, 35, 48));
        static HBRUSH hLightPillBg = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hDarkPillBg : hLightPillBg);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBm = SelectObject(hdcMem, hbm);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            bool isDark = (self && self->m_darkMode);
            Gdiplus::Color windowBg = isDark ? Gdiplus::Color(255, 20, 24, 33) : Gdiplus::Color(255, 244, 247, 252);
            Gdiplus::Color pillBg   = isDark ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 255, 255, 255);
            Gdiplus::Color border   = isDark ? Gdiplus::Color(255, 54, 62, 82) : Gdiplus::Color(255, 215, 222, 235);
            Gdiplus::Color iconCol  = isDark ? Gdiplus::Color(255, 150, 160, 185) : Gdiplus::Color(255, 120, 130, 150);

            g.Clear(windowBg);

            Gdiplus::RectF rect(0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f);
            Gdiplus::GraphicsPath path;
            Utils::AddRoundedRect(path, rect, 7.0f);

            Gdiplus::SolidBrush bgBrush(pillBg);
            g.FillPath(&bgBrush, &path);

            Gdiplus::Pen bPen(border, 1.0f);
            g.DrawPath(&bPen, &path);

            // Magnifying glass icon
            Gdiplus::Pen iconPen(iconCol, 1.6f);
            g.DrawEllipse(&iconPen, 8.0f, 6.5f, 9.5f, 9.5f);
            g.DrawLine(&iconPen, 15.5f, 14.5f, 19.5f, 18.5f);
        }

        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, oldBm);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK WatchlistWindow::TableWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WatchlistWindow* self = reinterpret_cast<WatchlistWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        if (pt.y < 30) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    }
    case WM_MOUSEWHEEL: {
        if (!self || self->m_filteredPlayers.empty()) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        float rowsDelta = -(float)delta / 120.0f * 3.0f;

        RECT rc;
        GetClientRect(hWnd, &rc);
        int h = rc.bottom - rc.top;
        int visibleRows = std::max(1, (h - 30) / 32);
        int maxOffset = std::max(0, (int)self->m_filteredPlayers.size() - visibleRows);

        self->m_targetScrollY = std::clamp(self->m_targetScrollY + rowsDelta, 0.0f, (float)maxOffset);
        self->m_scrollAnimating = true;
        SetTimer(hWnd, 2, 16, nullptr);
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 2 && self && self->m_scrollAnimating) {
            float diff = self->m_targetScrollY - self->m_currentScrollY;
            if (std::abs(diff) < 0.02f) {
                self->m_currentScrollY = self->m_targetScrollY;
                self->m_scrollOffset = (int)std::round(self->m_currentScrollY);
                self->m_scrollAnimating = false;
                KillTimer(hWnd, 2);
            } else {
                self->m_currentScrollY += diff * 0.30f;
                self->m_scrollOffset = (int)std::round(self->m_currentScrollY);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(hWnd);
        if (self) {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);

            // Column header click to sort
            if (y < 30) {
                RECT rc;
                GetClientRect(hWnd, &rc);
                int w = rc.right - rc.left;

                float colX_Clan = std::max(480.0f, (float)(w - 180));
                float colX_Id   = std::max(290.0f, colX_Clan - 190.0f);
                float colX_User = 100.0f;
                float colX_Stat = 14.0f;

                WatchlistSortCol clickedCol = WatchlistSortCol::Username;
                if (x < colX_User) {
                    clickedCol = WatchlistSortCol::Status;
                } else if (x < colX_Id) {
                    clickedCol = WatchlistSortCol::Username;
                } else if (x < colX_Clan) {
                    clickedCol = WatchlistSortCol::RobloxId;
                } else {
                    clickedCol = WatchlistSortCol::Clan;
                }

                if (self->m_sortColumn == clickedCol) {
                    self->m_sortAscending = !self->m_sortAscending;
                } else {
                    self->m_sortColumn = clickedCol;
                    self->m_sortAscending = true;
                }

                self->SortFilteredPlayers();
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }

            if (!self->m_filteredPlayers.empty() && y >= 30) {
                int clickedRow = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 32.0f);
                if (clickedRow >= 0 && clickedRow < (int)self->m_filteredPlayers.size()) {
                    self->m_selectedRow = clickedRow;
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        SetFocus(hWnd);
        if (self && !self->m_filteredPlayers.empty()) {
            int y = (short)HIWORD(lParam);
            if (y >= 30) {
                int clickedRow = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 32.0f);
                if (clickedRow >= 0 && clickedRow < (int)self->m_filteredPlayers.size()) {
                    self->m_selectedRow = clickedRow;
                    self->OpenSelectedProfile();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        if (self && !self->m_filteredPlayers.empty()) {
            int y = (short)HIWORD(lParam);
            if (y >= 30) {
                int clickedRow = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 32.0f);
                if (clickedRow >= 0 && clickedRow < (int)self->m_filteredPlayers.size()) {
                    self->m_selectedRow = clickedRow;
                    InvalidateRect(hWnd, nullptr, FALSE);

                    POINT pt;
                    GetCursorPos(&pt);

                    const auto& p = self->m_filteredPlayers[self->m_selectedRow];

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, IDM_WL_CTX_OPEN_PROFILE, L"Open Roblox Profile\t(Double click)");
                    AppendMenuW(hMenu, MF_STRING, IDM_WL_CTX_COPY_PROFILE_URL, L"Copy Profile Link");
                    AppendMenuW(hMenu, MF_STRING, IDM_WL_CTX_COPY_USERNAME, L"Copy Username");
                    if (p.roblox_id > 0) {
                        AppendMenuW(hMenu, MF_STRING, IDM_WL_CTX_COPY_USER_ID, (L"Copy Roblox ID (" + std::to_wstring(p.roblox_id) + L")").c_str());
                    }
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hMenu, MF_STRING, IDM_WL_CTX_COPY_FULL_INFO, L"Copy Full Details");

                    int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
                    DestroyMenu(hMenu);

                    if (cmd == IDM_WL_CTX_OPEN_PROFILE) {
                        self->OpenSelectedProfile();
                    } else if (cmd == IDM_WL_CTX_COPY_PROFILE_URL) {
                        std::wstring profileUrl = (p.roblox_id > 0)
                            ? (L"https://www.roblox.com/users/" + std::to_wstring(p.roblox_id) + L"/profile")
                            : (L"https://www.roblox.com/search/users?keyword=" + p.username);
                        Utils::CopyTextToClipboard(self->m_hWnd, profileUrl);
                        self->ShowStatusMessage(L"\u2713 Copied profile link!");
                    } else if (cmd == IDM_WL_CTX_COPY_USERNAME) {
                        Utils::CopyTextToClipboard(self->m_hWnd, p.username);
                        self->ShowStatusMessage(L"\u2713 Copied '" + p.username + L"'!");
                    } else if (cmd == IDM_WL_CTX_COPY_USER_ID) {
                        Utils::CopyTextToClipboard(self->m_hWnd, std::to_wstring(p.roblox_id));
                        self->ShowStatusMessage(L"\u2713 Copied Roblox ID " + std::to_wstring(p.roblox_id) + L"!");
                    } else if (cmd == IDM_WL_CTX_COPY_FULL_INFO) {
                        std::wstring info = p.username;
                        if (p.roblox_id > 0) {
                            info += L" [ID: " + std::to_wstring(p.roblox_id) + L"]";
                        }
                        info += L" (" + p.status + (p.clan.empty() ? L"" : L" / " + p.clan) + L")";
                        Utils::CopyTextToClipboard(self->m_hWnd, info);
                        self->ShowStatusMessage(L"\u2713 Copied full player details!");
                    }
                }
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);

        if (self) {
            int y = (short)HIWORD(lParam);
            int newHover = -1;
            if (y >= 30 && !self->m_filteredPlayers.empty()) {
                int rowIdx = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 32.0f);
                if (rowIdx >= 0 && rowIdx < (int)self->m_filteredPlayers.size()) {
                    newHover = rowIdx;
                }
            }
            if (newHover != self->m_hoveredRow) {
                self->m_hoveredRow = newHover;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (self && self->m_hoveredRow != -1) {
            self->m_hoveredRow = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBm = SelectObject(hdcMem, hbm);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            bool isDark = (self && self->m_darkMode);

            Gdiplus::Color cardBg    = isDark ? Gdiplus::Color(255, 30, 35, 48)   : Gdiplus::Color(255, 255, 255, 255);
            Gdiplus::Color borderCol = isDark ? Gdiplus::Color(255, 50, 58, 76)   : Gdiplus::Color(255, 224, 230, 240);
            Gdiplus::Color headerBg  = isDark ? Gdiplus::Color(255, 25, 30, 42)   : Gdiplus::Color(255, 238, 242, 249);
            Gdiplus::Color altRowBg  = isDark ? Gdiplus::Color(255, 34, 40, 54)   : Gdiplus::Color(255, 248, 250, 253);
            Gdiplus::Color hoverBg   = isDark ? Gdiplus::Color(255, 44, 52, 70)   : Gdiplus::Color(255, 237, 242, 250);
            Gdiplus::Color selBg     = isDark ? Gdiplus::Color(255, 52, 65, 96)   : Gdiplus::Color(255, 222, 233, 255);
            Gdiplus::Color lineCol   = isDark ? Gdiplus::Color(255, 42, 49, 64)   : Gdiplus::Color(255, 241, 243, 247);
            Gdiplus::Color textPrim  = isDark ? Gdiplus::Color(255, 243, 245, 252) : Gdiplus::Color(255, 32, 37, 52);
            Gdiplus::Color textSec   = isDark ? Gdiplus::Color(255, 154, 165, 188) : Gdiplus::Color(255, 126, 139, 159);
            Gdiplus::Color sortIndCol= isDark ? Gdiplus::Color(255, 85, 117, 239)  : Gdiplus::Color(255, 66, 97, 220);

            g.Clear(cardBg);

            // header bar
            Gdiplus::SolidBrush hBrush(headerBg);
            g.FillRectangle(&hBrush, 0.0f, 0.0f, (float)w, 30.0f);

            Gdiplus::Pen sepPen(borderCol, 1.0f);
            g.DrawLine(&sepPen, 0.0f, 30.0f, (float)w, 30.0f);

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font headerFont(&ff, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush headerTxtBrush(textSec);
            Gdiplus::SolidBrush sortTxtBrush(sortIndCol);

            Gdiplus::StringFormat sfNear;
            sfNear.SetAlignment(Gdiplus::StringAlignmentNear);
            sfNear.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            sfNear.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            sfNear.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

            float colX_Clan = std::max(480.0f, (float)(w - 180));
            float colX_Id   = std::max(290.0f, colX_Clan - 190.0f);
            float colX_User = 100.0f;
            float colX_Stat = 14.0f;

            auto getSortSuffix = [self](WatchlistSortCol col) -> std::wstring {
                if (self && self->m_sortColumn == col) {
                    return self->m_sortAscending ? L" \u25B2" : L" \u25BC";
                }
                return L"";
            };

            auto drawHeader = [&](const std::wstring& title, WatchlistSortCol col, float x, float width) {
                std::wstring full = title + getSortSuffix(col);
                bool isActive = (self && self->m_sortColumn == col);
                g.DrawString(full.c_str(), -1, &headerFont, Gdiplus::RectF(x, 0.0f, width, 30.0f), &sfNear, isActive ? &sortTxtBrush : &headerTxtBrush);
            };

            drawHeader(L"STATUS", WatchlistSortCol::Status, colX_Stat, 75.0f);
            drawHeader(L"USERNAME", WatchlistSortCol::Username, colX_User, colX_Id - colX_User - 10.0f);
            drawHeader(L"ROBLOX ID", WatchlistSortCol::RobloxId, colX_Id, colX_Clan - colX_Id - 10.0f);
            drawHeader(L"CLAN", WatchlistSortCol::Clan, colX_Clan, (float)(w - colX_Clan - 10));

            // rows
            if (!self || self->m_filteredPlayers.empty()) {
                Gdiplus::Font emptyFont(&ff, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush emptyBrush(textSec);
                Gdiplus::StringFormat sfCenter;
                sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
                sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                std::wstring emptyMsg = (self && self->m_allPlayers.empty())
                    ? L"Watchlist is empty."
                    : L"No players match the current search / filter.";

                g.DrawString(emptyMsg.c_str(), -1, &emptyFont,
                             Gdiplus::RectF(0.0f, 30.0f, (float)w, (float)(h - 30)), &sfCenter, &emptyBrush);
            } else {
                int rowH = 32;
                int visibleRows = std::max(1, (h - 30) / rowH);
                int maxOffset = std::max(0, (int)self->m_filteredPlayers.size() - visibleRows);
                self->m_currentScrollY = std::clamp(self->m_currentScrollY, 0.0f, (float)maxOffset);

                // clip rows area
                g.SetClip(Gdiplus::RectF(0.0f, 30.0f, (float)w, (float)(h - 30)));

                Gdiplus::Font fontUser(&ff, 9.2f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fontId(&ff, 8.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::Font fontClan(&ff, 8.2f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fontBadge(&ff, 7.8f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);

                Gdiplus::SolidBrush userBrush(textPrim);
                Gdiplus::SolidBrush idBrush(textSec);
                Gdiplus::SolidBrush clanBrush(textSec);
                Gdiplus::Pen rowLinePen(lineCol, 1.0f);

                Gdiplus::StringFormat sfBadge;
                sfBadge.SetAlignment(Gdiplus::StringAlignmentCenter);
                sfBadge.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                int startIdx = (int)std::floor(self->m_currentScrollY);
                int endIdx = std::min((int)self->m_filteredPlayers.size(), startIdx + visibleRows + 2);

                for (int i = startIdx; i < endIdx; ++i) {
                    float curY = 30.0f + ((float)i - self->m_currentScrollY) * (float)rowH;
                    if (curY + (float)rowH < 30.0f || curY >(float)h) continue;

                    const auto& p = self->m_filteredPlayers[i];

                    // background highlight
                    if (i == self->m_selectedRow) {
                        Gdiplus::SolidBrush b(selBg);
                        g.FillRectangle(&b, 0.0f, curY, (float)w, (float)rowH);
                    } else if (i == self->m_hoveredRow) {
                        Gdiplus::SolidBrush b(hoverBg);
                        g.FillRectangle(&b, 0.0f, curY, (float)w, (float)rowH);
                    } else if (i % 2 == 1) {
                        Gdiplus::SolidBrush b(altRowBg);
                        g.FillRectangle(&b, 0.0f, curY, (float)w, (float)rowH);
                    }

                    // bottom separator line
                    g.DrawLine(&rowLinePen, 0.0f, curY + (float)rowH, (float)w, curY + (float)rowH);

                    // Status Pill
                    bool isEnemy = (p.status == L"Enemy");
                    Gdiplus::Color pillBgCol = isEnemy
                        ? (isDark ? Gdiplus::Color(255, 60, 24, 28) : Gdiplus::Color(255, 255, 235, 235))
                        : (isDark ? Gdiplus::Color(255, 18, 50, 32) : Gdiplus::Color(255, 232, 250, 236));
                    Gdiplus::Color pillTxtCol = isEnemy
                        ? (isDark ? Gdiplus::Color(255, 255, 105, 115) : Gdiplus::Color(255, 215, 38, 56))
                        : (isDark ? Gdiplus::Color(255, 64, 210, 110) : Gdiplus::Color(255, 36, 148, 64));

                    Gdiplus::RectF pillRect(colX_Stat, curY + 6.0f, 68.0f, 20.0f);
                    Gdiplus::GraphicsPath pillPath;
                    Utils::AddRoundedRect(pillPath, pillRect, 6.0f);
                    Gdiplus::SolidBrush pillBgBrush(pillBgCol);
                    g.FillPath(&pillBgBrush, &pillPath);

                    Gdiplus::SolidBrush pillTxtBrush(pillTxtCol);
                    g.DrawString(isEnemy ? L"ENEMY" : L"ALLY", -1, &fontBadge, pillRect, &sfBadge, &pillTxtBrush);

                    // Username
                    Gdiplus::RectF userRect(colX_User, curY, colX_Id - colX_User - 10.0f, (float)rowH);
                    g.DrawString(p.username.c_str(), -1, &fontUser, userRect, &sfNear, &userBrush);

                    // Roblox ID
                    std::wstring idStr = (p.roblox_id > 0) ? std::to_wstring(p.roblox_id) : L"\u2014";
                    Gdiplus::RectF idRect(colX_Id, curY, colX_Clan - colX_Id - 10.0f, (float)rowH);
                    g.DrawString(idStr.c_str(), -1, &fontId, idRect, &sfNear, &idBrush);

                    // Clan
                    std::wstring clanStr = p.clan.empty() ? L"\u2014" : p.clan;
                    Gdiplus::RectF clanRect(colX_Clan, curY, (float)(w - colX_Clan - 10), (float)rowH);
                    g.DrawString(clanStr.c_str(), -1, &fontClan, clanRect, &sfNear, &clanBrush);
                }

                g.ResetClip();

                // Scrollbar indicator track on the right
                if (maxOffset > 0) {
                    float trackH = (float)(h - 30);
                    float thumbH = std::max(20.0f, trackH * ((float)visibleRows / (float)self->m_filteredPlayers.size()));
                    float thumbY = 30.0f + (trackH - thumbH) * (self->m_currentScrollY / (float)maxOffset);

                    Gdiplus::RectF thumbRect((float)w - 6.0f, thumbY, 4.0f, thumbH);
                    Gdiplus::GraphicsPath thumbPath;
                    Utils::AddRoundedRect(thumbPath, thumbRect, 2.0f);
                    Gdiplus::SolidBrush thumbBrush(isDark ? Gdiplus::Color(100, 180, 190, 210) : Gdiplus::Color(100, 140, 150, 170));
                    g.FillPath(&thumbBrush, &thumbPath);
                }
            }
        }

        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, oldBm);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void WatchlistWindow::UpdateData(const std::vector<PlayerRecord>& players, const Config& config) {
    m_allPlayers = players;
    m_darkMode = config.appearance.dark_mode;

    // update filter counts
    int totalCount = (int)m_allPlayers.size();
    int enemiesCount = 0;
    int alliesCount = 0;
    for (const auto& p : m_allPlayers) {
        if (p.status == L"Enemy") enemiesCount++;
        else if (p.status == L"Ally") alliesCount++;
    }

    if (m_hFilterAllBtn && IsWindow(m_hFilterAllBtn)) {
        std::wstring text = L"All (" + std::to_wstring(totalCount) + L")";
        SetWindowTextW(m_hFilterAllBtn, text.c_str());
        InvalidateRect(m_hFilterAllBtn, nullptr, TRUE);
    }
    if (m_hFilterEnemiesBtn && IsWindow(m_hFilterEnemiesBtn)) {
        std::wstring text = L"Enemies (" + std::to_wstring(enemiesCount) + L")";
        SetWindowTextW(m_hFilterEnemiesBtn, text.c_str());
        InvalidateRect(m_hFilterEnemiesBtn, nullptr, TRUE);
    }
    if (m_hFilterAlliesBtn && IsWindow(m_hFilterAlliesBtn)) {
        std::wstring text = L"Allies (" + std::to_wstring(alliesCount) + L")";
        SetWindowTextW(m_hFilterAlliesBtn, text.c_str());
        InvalidateRect(m_hFilterAlliesBtn, nullptr, TRUE);
    }

    ApplyFilter();
    ApplyTheme(config);
}

void WatchlistWindow::ApplyTheme(const Config& config) {
    m_darkMode = config.appearance.dark_mode;
    if (m_hWnd && IsWindow(m_hWnd)) {
        BOOL dark = m_darkMode ? TRUE : FALSE;
        DwmSetWindowAttribute(m_hWnd, 20, &dark, sizeof(dark));
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
    if (m_hTable && IsWindow(m_hTable)) {
        InvalidateRect(m_hTable, nullptr, TRUE);
    }
    if (m_hSearchContainer && IsWindow(m_hSearchContainer)) {
        InvalidateRect(m_hSearchContainer, nullptr, TRUE);
    }
}

void WatchlistWindow::Show(const std::vector<PlayerRecord>& players, const Config& config) {
    m_darkMode = config.appearance.dark_mode;

    if (!m_hWnd) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = WL_WINDOW_CLASS_NAME;
        RegisterClassExW(&wc);

        WNDCLASSEXW twc = { sizeof(WNDCLASSEXW) };
        twc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        twc.lpfnWndProc = TableWndProc;
        twc.hInstance = m_hInstance;
        twc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        twc.hbrBackground = nullptr;
        twc.lpszClassName = WL_TABLE_CLASS_NAME;
        RegisterClassExW(&twc);

        WNDCLASSEXW pwc = { sizeof(WNDCLASSEXW) };
        pwc.style = CS_HREDRAW | CS_VREDRAW;
        pwc.lpfnWndProc = SearchPillWndProc;
        pwc.hInstance = m_hInstance;
        pwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        pwc.hbrBackground = nullptr;
        pwc.lpszClassName = WL_PILL_CLASS_NAME;
        RegisterClassExW(&pwc);

        HWND testBtn = CreateWindowExW(0, L"BUTTON", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, m_hInstance, nullptr);
        s_oldWlBtnProc = (WNDPROC)GetWindowLongPtrW(testBtn, GWLP_WNDPROC);
        DestroyWindow(testBtn);

        m_hFontRegular = CreateFontW(
            -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        m_hFontBold = CreateFontW(
            -12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );

        // Center on parent window or desktop
        RECT parentRc;
        int winX = CW_USEDEFAULT, winY = CW_USEDEFAULT;
        if (m_hParent && IsWindow(m_hParent)) {
            GetWindowRect(m_hParent, &parentRc);
            winX = parentRc.left + 30;
            winY = parentRc.top + 30;
        }

        m_hWnd = CreateWindowExW(
            WS_EX_APPWINDOW,
            WL_WINDOW_CLASS_NAME,
            L"Watchlist Database \u2014 BorderWatch",
            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
            winX, winY, 860, 580,
            nullptr, nullptr, m_hInstance, this
        );

        int corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        BOOL dark = m_darkMode ? TRUE : FALSE;
        DwmSetWindowAttribute(m_hWnd, 20, &dark, sizeof(dark));

        // Close button in header
        m_hTopCloseBtn = CreateWindowExW(
            0, L"BUTTON", L"\u2715",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            860 - 42, 14, 28, 28,
            m_hWnd, (HMENU)IDC_WL_BTN_TOP_CLOSE, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hTopCloseBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        // Filter Pills
        m_hFilterAllBtn = CreateWindowExW(
            0, L"BUTTON", L"All",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            18, 62, 75, 27,
            m_hWnd, (HMENU)IDC_WL_FILTER_ALL, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hFilterAllBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        m_hFilterEnemiesBtn = CreateWindowExW(
            0, L"BUTTON", L"Enemies",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            99, 62, 100, 27,
            m_hWnd, (HMENU)IDC_WL_FILTER_ENEMIES, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hFilterEnemiesBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        m_hFilterAlliesBtn = CreateWindowExW(
            0, L"BUTTON", L"Allies",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            205, 62, 90, 27,
            m_hWnd, (HMENU)IDC_WL_FILTER_ALLIES, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hFilterAlliesBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        // Search container pill
        m_hSearchContainer = CreateWindowExW(
            0, WL_PILL_CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE,
            305, 62, 860 - 18 - 305, 27,
            m_hWnd, nullptr, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hSearchContainer, GWLP_USERDATA, (LONG_PTR)this);

        // Search edit control inside search pill
        m_hSearchEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            28, 4, 300, 19,
            m_hSearchContainer, (HMENU)IDC_WL_EDT_SEARCH, m_hInstance, nullptr
        );
        SendMessageW(m_hSearchEdit, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
        SendMessageW(m_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search username, clan, ID...");

        // Table
        m_hTable = CreateWindowExW(
            0, WL_TABLE_CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            18, 96, 860 - 36, 580 - 146,
            m_hWnd, nullptr, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hTable, GWLP_USERDATA, (LONG_PTR)this);

        // Footer buttons
        m_hProfileBtn = CreateWindowExW(
            0, L"BUTTON", L"Open Profile",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            18, 580 - 42, 110, 32,
            m_hWnd, (HMENU)IDC_WL_BTN_PROFILE, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hProfileBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        m_hCopyUserBtn = CreateWindowExW(
            0, L"BUTTON", L"Copy Name",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            136, 580 - 42, 100, 32,
            m_hWnd, (HMENU)IDC_WL_BTN_COPY_USER, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hCopyUserBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        m_hCopyIdBtn = CreateWindowExW(
            0, L"BUTTON", L"Copy ID",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            244, 580 - 42, 90, 32,
            m_hWnd, (HMENU)IDC_WL_BTN_COPY_ID, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hCopyIdBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        m_hExportBtn = CreateWindowExW(
            0, L"BUTTON", L"Export CSV",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            342, 580 - 42, 100, 32,
            m_hWnd, (HMENU)IDC_WL_BTN_EXPORT, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hExportBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        m_hRefreshBtn = CreateWindowExW(
            0, L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            860 - 18 - 85, 580 - 42, 85, 32,
            m_hWnd, (HMENU)IDC_WL_REFRESH, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hRefreshBtn, GWLP_WNDPROC, (LONG_PTR)WlHandCursorBtnSubclass);

        // Tooltips
        m_hTooltip = CreateWindowExW(
            WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            m_hWnd, nullptr, m_hInstance, nullptr
        );
        if (m_hTooltip) {
            SetWindowPos(m_hTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            TOOLINFOW ti = { sizeof(TOOLINFOW) };
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = m_hWnd;

            ti.uId = (UINT_PTR)m_hExportBtn;
            ti.lpszText = const_cast<LPWSTR>(L"Export current filtered and sorted watchlist to a CSV file on Desktop.");
            SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)m_hProfileBtn;
            ti.lpszText = const_cast<LPWSTR>(L"Open selected Roblox player's profile in default browser.");
            SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
    }

    UpdateData(players, config);
    ShowWindow(m_hWnd, SW_SHOW);
    SetForegroundWindow(m_hWnd);
}

void WatchlistWindow::Hide() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

bool WatchlistWindow::IsVisible() const {
    return m_hWnd && IsWindow(m_hWnd) && IsWindowVisible(m_hWnd);
}

void WatchlistWindow::RelayoutControls(int w, int h) {
    if (m_hTopCloseBtn && IsWindow(m_hTopCloseBtn)) {
        SetWindowPos(m_hTopCloseBtn, nullptr, w - 42, 14, 28, 28, SWP_NOZORDER);
    }
    if (m_hSearchContainer && IsWindow(m_hSearchContainer)) {
        int pillW = std::max(120, w - 18 - 305);
        SetWindowPos(m_hSearchContainer, nullptr, 305, 62, pillW, 27, SWP_NOZORDER);
        if (m_hSearchEdit && IsWindow(m_hSearchEdit)) {
            SetWindowPos(m_hSearchEdit, nullptr, 28, 4, std::max(60, pillW - 36), 19, SWP_NOZORDER);
        }
    }
    int btnY = h - 42;
    int tableH = std::max(60, btnY - 104);
    int tableW = std::max(100, w - 36);
    if (m_hTable && IsWindow(m_hTable)) {
        SetWindowPos(m_hTable, nullptr, 18, 96, tableW, tableH, SWP_NOZORDER);
        InvalidateRect(m_hTable, nullptr, FALSE);
    }
    if (m_hProfileBtn && IsWindow(m_hProfileBtn)) {
        SetWindowPos(m_hProfileBtn, nullptr, 18, btnY, 110, 32, SWP_NOZORDER);
    }
    if (m_hCopyUserBtn && IsWindow(m_hCopyUserBtn)) {
        SetWindowPos(m_hCopyUserBtn, nullptr, 136, btnY, 100, 32, SWP_NOZORDER);
    }
    if (m_hCopyIdBtn && IsWindow(m_hCopyIdBtn)) {
        SetWindowPos(m_hCopyIdBtn, nullptr, 244, btnY, 90, 32, SWP_NOZORDER);
    }
    if (m_hExportBtn && IsWindow(m_hExportBtn)) {
        SetWindowPos(m_hExportBtn, nullptr, 342, btnY, 100, 32, SWP_NOZORDER);
    }
    int refreshW = 85;
    int refreshX = w - 18 - refreshW;
    if (m_hRefreshBtn && IsWindow(m_hRefreshBtn)) {
        SetWindowPos(m_hRefreshBtn, nullptr, refreshX, btnY, refreshW, 32, SWP_NOZORDER);
    }
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

LRESULT CALLBACK WatchlistWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WatchlistWindow* self = reinterpret_cast<WatchlistWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 680;
        mmi->ptMinTrackSize.y = 420;
        return 0;
    }
    case WM_SIZE: {
        if (self) {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            self->RelayoutControls(w, h);
        }
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        const int b = 7;

        if (pt.y < b && pt.x < b) return HTTOPLEFT;
        if (pt.y < b && pt.x > w - b) return HTTOPRIGHT;
        if (pt.y > h - b && pt.x < b) return HTBOTTOMLEFT;
        if (pt.y > h - b && pt.x > w - b) return HTBOTTOMRIGHT;
        if (pt.y < b) return HTTOP;
        if (pt.y > h - b) return HTBOTTOM;
        if (pt.x < b) return HTLEFT;
        if (pt.x > w - b) return HTRIGHT;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        if (y <= 54 && x < (self ? (GetSystemMetrics(SM_CXSCREEN)) : 400)) {
            // Drag caption unless clicking close button
            RECT rcClose;
            if (self && self->m_hTopCloseBtn && IsWindow(self->m_hTopCloseBtn)) {
                GetWindowRect(self->m_hTopCloseBtn, &rcClose);
                POINT p = { x, y };
                ClientToScreen(hWnd, &p);
                if (PtInRect(&rcClose, p)) break;
            }
            ReleaseCapture();
            SendMessageW(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        bool isDark = (self && self->m_darkMode);
        COLORREF bg = isDark ? RGB(30, 35, 48) : RGB(255, 255, 255);
        COLORREF text = isDark ? RGB(241, 243, 250) : RGB(36, 41, 56);
        SetBkColor(hdc, bg);
        SetTextColor(hdc, text);
        static HBRUSH hDarkEditBg = CreateSolidBrush(RGB(30, 35, 48));
        static HBRUSH hLightEditBg = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hDarkEditBg : hLightEditBg);
    }
    case WM_TIMER: {
        if (wParam == 998 && self) {
            KillTimer(hWnd, 998);
            self->m_statusMsg.clear();
            RECT rc;
            GetClientRect(hWnd, &rc);
            RECT rcFooter = { 18, rc.bottom - 44, rc.right - 18, rc.bottom };
            InvalidateRect(hWnd, &rcFooter, FALSE);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis || !self) break;

        int w = dis->rcItem.right - dis->rcItem.left;
        int h = dis->rcItem.bottom - dis->rcItem.top;

        HDC hdcMem = CreateCompatibleDC(dis->hDC);
        HBITMAP hbm = CreateCompatibleBitmap(dis->hDC, w, h);
        HGDIOBJ oldBm = SelectObject(hdcMem, hbm);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            bool isDark = self->m_darkMode;
            Gdiplus::Color windowBg = isDark ? Gdiplus::Color(255, 20, 24, 33) : Gdiplus::Color(255, 244, 247, 252);
            g.Clear(windowBg);

            bool isPressed = (dis->itemState & ODS_SELECTED);
            bool isHovered = (GetPropW(dis->hwndItem, L"Hover") != nullptr);
            int id = (int)dis->CtlID;

            Gdiplus::RectF rect(0, 0, (float)w, (float)h);
            Gdiplus::GraphicsPath path;

            Gdiplus::Color bgColor, textColor, borderColor;
            bool hasBorder = false;

            if (id == IDC_WL_BTN_TOP_CLOSE) {
                Utils::AddRoundedRect(path, rect, 7.0f);
                if (isPressed || isHovered) {
                    bgColor = isDark ? (isPressed ? Gdiplus::Color(255, 110, 40, 52) : Gdiplus::Color(255, 235, 72, 77))
                                     : (isPressed ? Gdiplus::Color(255, 220, 55, 60) : Gdiplus::Color(255, 235, 72, 77));
                    textColor = Gdiplus::Color(255, 255, 255, 255);
                } else {
                    bgColor = windowBg;
                    textColor = isDark ? Gdiplus::Color(255, 161, 172, 194) : Gdiplus::Color(255, 126, 139, 159);
                }
                hasBorder = false;
            } else if (id == IDC_WL_FILTER_ALL || id == IDC_WL_FILTER_ENEMIES || id == IDC_WL_FILTER_ALLIES) {
                Utils::AddRoundedRect(path, rect, 7.0f);
                bool isActive = (id == IDC_WL_FILTER_ALL && self->m_filterMode == 0) ||
                                (id == IDC_WL_FILTER_ENEMIES && self->m_filterMode == 1) ||
                                (id == IDC_WL_FILTER_ALLIES && self->m_filterMode == 2);

                if (isActive) {
                    bgColor = isPressed ? Gdiplus::Color(255, 66, 97, 220) : Gdiplus::Color(255, 85, 117, 239);
                    textColor = Gdiplus::Color(255, 255, 255, 255);
                } else {
                    bgColor = isDark ? (isPressed ? Gdiplus::Color(255, 34, 39, 52) : Gdiplus::Color(255, 42, 48, 64))
                                     : (isPressed ? Gdiplus::Color(255, 232, 236, 244) : Gdiplus::Color(255, 240, 244, 250));
                    textColor = isDark ? Gdiplus::Color(255, 180, 190, 210) : Gdiplus::Color(255, 70, 80, 100);
                    borderColor = isDark ? Gdiplus::Color(255, 58, 68, 88) : Gdiplus::Color(255, 215, 222, 235);
                    hasBorder = true;
                }
            } else if (id == IDC_WL_REFRESH) {
                Utils::AddRoundedRect(path, rect, 8.0f);
                bgColor = isPressed ? Gdiplus::Color(255, 66, 97, 220) : Gdiplus::Color(255, 85, 117, 239);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else {
                Utils::AddRoundedRect(path, rect, 8.0f);
                if (isDark) {
                    bgColor = isPressed ? Gdiplus::Color(255, 38, 44, 58) : Gdiplus::Color(255, 48, 55, 72);
                    textColor = Gdiplus::Color(255, 223, 229, 242);
                    borderColor = Gdiplus::Color(255, 62, 72, 94);
                } else {
                    bgColor = isPressed ? Gdiplus::Color(255, 226, 230, 239) : Gdiplus::Color(255, 237, 240, 246);
                    textColor = Gdiplus::Color(255, 55, 64, 86);
                    borderColor = Gdiplus::Color(255, 215, 222, 235);
                }
                hasBorder = true;
            }

            Gdiplus::SolidBrush bgBrush(bgColor);
            g.FillPath(&bgBrush, &path);

            if (hasBorder) {
                Gdiplus::Pen bPen(borderColor, 1.0f);
                g.DrawPath(&bPen, &path);
            }

            wchar_t text[128] = { 0 };
            GetWindowTextW(dis->hwndItem, text, 128);

            Gdiplus::FontFamily ff(L"Segoe UI");
            bool isTopBtn = (id == IDC_WL_BTN_TOP_CLOSE);
            float fontSize = isTopBtn ? 12.5f : 9.0f;
            bool isBold = (id == IDC_WL_REFRESH || isTopBtn);
            Gdiplus::Font font(&ff, fontSize, isBold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush textBrush(textColor);

            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(text, -1, &font, rect, &sf, &textBrush);
        }

        BitBlt(dis->hDC, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, oldBm);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBm = SelectObject(hdcMem, hbm);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            bool isDark = (self && self->m_darkMode);
            Gdiplus::Color bgColor = isDark ? Gdiplus::Color(255, 20, 24, 33) : Gdiplus::Color(255, 244, 247, 252);
            Gdiplus::Color titleColor = isDark ? Gdiplus::Color(255, 243, 245, 252) : Gdiplus::Color(255, 32, 37, 52);
            Gdiplus::Color subColor = isDark ? Gdiplus::Color(255, 154, 165, 188) : Gdiplus::Color(255, 126, 136, 158);
            Gdiplus::Color accentCol = isDark ? Gdiplus::Color(255, 85, 117, 239) : Gdiplus::Color(255, 66, 97, 220);
            g.Clear(bgColor);

            // Badge [B]
            Gdiplus::RectF badgeRect(18, 14, 34, 34);
            Gdiplus::GraphicsPath badgePath;
            Utils::AddRoundedRect(badgePath, badgeRect, 9.0f);
            Gdiplus::SolidBrush badgeBrush(Gdiplus::Color(255, 85, 117, 239));
            g.FillPath(&badgeBrush, &badgePath);
            Gdiplus::Pen badgeRing(Gdiplus::Color(70, 255, 255, 255), 1.0f);
            g.DrawPath(&badgeRing, &badgePath);

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font bFont(&ff, 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat sfCenter;
            sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
            sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(L"B", -1, &bFont, badgeRect, &sfCenter, &whiteBrush);

            // Header Title
            Gdiplus::Font titleFont(&ff, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush titleBrush(titleColor);
            g.DrawString(L"Watchlist Database", -1, &titleFont, Gdiplus::PointF(60, 11), &titleBrush);

            // Header Subtitle: Player Counts & Sorting Info
            if (self) {
                int totalCount = (int)self->m_allPlayers.size();
                int enemies = 0;
                int allies = 0;
                for (const auto& p : self->m_allPlayers) {
                    if (p.status == L"Enemy") enemies++;
                    else if (p.status == L"Ally") allies++;
                }

                std::wstring subText = std::to_wstring(totalCount) + L" players total  \u00B7  " +
                                       std::to_wstring(enemies) + L" Enemies  \u00B7  " +
                                       std::to_wstring(allies) + L" Allies  \u00B7  Click headers to sort";
                Gdiplus::Font subFont(&ff, 8.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush subBrush(subColor);
                g.DrawString(subText.c_str(), -1, &subFont, Gdiplus::PointF(60, 31), &subBrush);

                // Footer status / showing count
                int btnY = h - 42;
                Gdiplus::Font footerFont(&ff, 8.8f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush footerBrush(subColor);
                Gdiplus::SolidBrush actBrush(accentCol);

                std::wstring footerInfo;
                if (!self->m_statusMsg.empty()) {
                    g.DrawString(self->m_statusMsg.c_str(), -1, &footerFont, Gdiplus::PointF(452.0f, (float)btnY + 8.0f), &actBrush);
                } else {
                    footerInfo = L"Showing " + std::to_wstring(self->m_filteredPlayers.size()) + L" of " + std::to_wstring(totalCount) + L" players";
                    g.DrawString(footerInfo.c_str(), -1, &footerFont, Gdiplus::PointF(452.0f, (float)btnY + 8.0f), &footerBrush);
                }
            }
        }

        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, oldBm);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (code == EN_CHANGE && id == IDC_WL_EDT_SEARCH) {
            if (self) {
                wchar_t buf[256] = { 0 };
                GetWindowTextW(self->m_hSearchEdit, buf, 256);
                self->m_searchQuery = buf;
                self->ApplyFilter();
            }
            return 0;
        }

        if (self) {
            switch (id) {
            case IDC_WL_BTN_TOP_CLOSE:
                ShowWindow(hWnd, SW_HIDE);
                break;
            case IDC_WL_FILTER_ALL:
                self->m_filterMode = 0;
                self->ApplyFilter();
                InvalidateRect(self->m_hFilterAllBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterEnemiesBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterAlliesBtn, nullptr, TRUE);
                break;
            case IDC_WL_FILTER_ENEMIES:
                self->m_filterMode = 1;
                self->ApplyFilter();
                InvalidateRect(self->m_hFilterAllBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterEnemiesBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterAlliesBtn, nullptr, TRUE);
                break;
            case IDC_WL_FILTER_ALLIES:
                self->m_filterMode = 2;
                self->ApplyFilter();
                InvalidateRect(self->m_hFilterAllBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterEnemiesBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterAlliesBtn, nullptr, TRUE);
                break;
            case IDC_WL_BTN_PROFILE:
                self->OpenSelectedProfile();
                break;
            case IDC_WL_BTN_COPY_USER:
                self->CopySelectedUsername();
                break;
            case IDC_WL_BTN_COPY_ID:
                self->CopySelectedId();
                break;
            case IDC_WL_BTN_EXPORT:
                self->ExportToCsv();
                break;
            case IDC_WL_REFRESH:
                // Signal parent to reload/refresh watchlist
                SendMessageW(self->m_hParent, WM_COMMAND, MAKEWPARAM(2001, 0), 0);
                self->ShowStatusMessage(L"Refreshed watchlist data.");
                break;
            }
        }
        break;
    }
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
