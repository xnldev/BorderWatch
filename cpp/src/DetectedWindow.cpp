#include "../include/DetectedWindow.h"
#include "../include/Utils.h"
#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <mmsystem.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

static const wchar_t* DETECTED_CLASS_NAME = L"BorderWatchDetectedClass";
static const wchar_t* TABLE_CLASS_NAME    = L"BorderWatchDetectedTable";
static const wchar_t* PILL_CLASS_NAME     = L"BorderWatchSearchPill";

enum DetectedControlId {
    IDC_BTN_REFRESH = 103,
    IDC_FILTER_ALL = 201,
    IDC_FILTER_ENEMIES = 202,
    IDC_FILTER_ALLIES = 203,
    IDC_EDT_SEARCH = 204,
    IDC_BTN_PROFILE = 205,
    IDC_BTN_COPY = 206,
    IDC_BTN_DEEP_LINK = 208,
    IDC_BTN_CLEAR_PLAYERS = 209,
    IDC_BTN_DET_TOP_MIN = 210,
    IDC_BTN_DET_TOP_CLOSE = 211
};

enum ContextMenuId {
    IDM_CTX_OPEN_PROFILE = 3001,
    IDM_CTX_COPY_PROFILE_URL = 3002,
    IDM_CTX_COPY_USERNAME = 3003,
    IDM_CTX_COPY_DISPLAY_NAME = 3004,
    IDM_CTX_COPY_USER_ID = 3005,
    IDM_CTX_COPY_SERVER_DEEP_LINK = 3008,
    IDM_CTX_COPY_USER_DEEP_LINK = 3009,
    IDM_CTX_COPY_FULL_INFO = 3006,
    IDM_CTX_REMOVE_PLAYER = 3007
};

static void OpenRobloxProfile(const std::wstring& username, uint64_t userId = 0) {
    if (userId > 0) {
        std::wstring url = L"https://www.roblox.com/users/" + std::to_wstring(userId) + L"/profile";
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (!username.empty()) {
        std::wstring url = L"https://www.roblox.com/search/users?keyword=" + username;
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

static WNDPROC s_oldBtnProc = nullptr;
static LRESULT CALLBACK HandCursorBtnSubclass(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    return CallWindowProcW(s_oldBtnProc, hWnd, msg, wParam, lParam);
}

DetectedWindow::DetectedWindow(HINSTANCE hInstance, HWND hParent)
    : m_hInstance(hInstance), m_hParent(hParent) {}

DetectedWindow::~DetectedWindow() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
    }
    if (m_hFontRegular) DeleteObject(m_hFontRegular);
    if (m_hFontBold) DeleteObject(m_hFontBold);
}

void DetectedWindow::ShowStatusMessage(const std::wstring& msg) {
    m_copiedText = msg;
    if (m_hWnd && IsWindow(m_hWnd)) {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        RECT rcHeader = { 200, 8, rc.right - 44, 54 };
        InvalidateRect(m_hWnd, &rcHeader, FALSE);
        SetTimer(m_hWnd, 999, 2600, nullptr);
    }
}

void DetectedWindow::CopySelectedUsername() {
    if (m_selectedRow >= 0 && m_selectedRow < (int)m_filteredPlayers.size()) {
        const auto& p = m_filteredPlayers[m_selectedRow];
        Utils::CopyTextToClipboard(m_hWnd, p.username);
        ShowStatusMessage(L"Copied '" + p.username + L"' to clipboard!");
    } else {
        ShowStatusMessage(L"Select a player first.");
    }
}

void DetectedWindow::OpenSelectedProfile() {
    if (m_selectedRow >= 0 && m_selectedRow < (int)m_filteredPlayers.size()) {
        const auto& p = m_filteredPlayers[m_selectedRow];
        OpenRobloxProfile(p.username, p.user_id);
        std::wstring nameToShow = p.display_name.empty() ? p.username : (p.display_name + L" (@" + p.username + L")");
        ShowStatusMessage(L"Opening " + nameToShow + L" profile...");
    } else {
        ShowStatusMessage(L"Select a player first.");
    }
}

void DetectedWindow::ApplyFilter() {
    m_filteredPlayers.clear();
    std::wstring queryLower = m_searchQuery;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::towlower);

    for (const auto& p : m_allPlayers) {
        // filter by status mode
        if (m_filterMode == 1 && p.status != L"Enemy") continue;
        if (m_filterMode == 2 && p.status != L"Ally") continue;

        // filter by search query
        if (!queryLower.empty()) {
            std::wstring uLower = p.username;
            std::transform(uLower.begin(), uLower.end(), uLower.begin(), ::towlower);

            std::wstring dLower = p.display_name;
            std::transform(dLower.begin(), dLower.end(), dLower.begin(), ::towlower);

            std::wstring idStr = (p.user_id > 0) ? std::to_wstring(p.user_id) : L"";

            if (uLower.find(queryLower) == std::wstring::npos &&
                dLower.find(queryLower) == std::wstring::npos &&
                idStr.find(queryLower) == std::wstring::npos) {
                continue;
            }
        }

        m_filteredPlayers.push_back(p);
    }

    if (m_selectedRow >= (int)m_filteredPlayers.size()) {
        m_selectedRow = -1;
    }
    m_scrollOffset = 0;
    m_currentScrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_scrollAnimating = false;

    if (m_hTable && IsWindow(m_hTable)) {
        KillTimer(m_hTable, 2);
        InvalidateRect(m_hTable, nullptr, FALSE);
    }
}

LRESULT CALLBACK DetectedWindow::TableWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DetectedWindow* self = reinterpret_cast<DetectedWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    case WM_DESTROY:
        if (self && self->m_scrollAnimating) {
            self->m_scrollAnimating = false;
            timeEndPeriod(1);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        if (self && !self->m_filteredPlayers.empty()) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int h = (int)(rc.bottom - rc.top);
            int visibleRows = std::max(1, (h - 30) / 34);
            int maxOffset = std::max(0, (int)self->m_filteredPlayers.size() - visibleRows);

            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float rowsToScroll = (float)delta / 120.0f * 2.5f;
            self->m_targetScrollY = std::clamp(self->m_targetScrollY - rowsToScroll, 0.0f, (float)maxOffset);

            if (!self->m_scrollAnimating) {
                self->m_scrollAnimating = true;
                LARGE_INTEGER li;
                QueryPerformanceCounter(&li);
                self->m_lastScrollQpc = li.QuadPart;
                timeBeginPeriod(1);
                SetTimer(hWnd, 2, 8, nullptr);
            }
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 2 && self && self->m_scrollAnimating) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            float dt = (float)(now.QuadPart - self->m_lastScrollQpc) / (float)freq.QuadPart;
            self->m_lastScrollQpc = now.QuadPart;
            dt = std::clamp(dt, 0.001f, 0.05f);

            float diff = self->m_targetScrollY - self->m_currentScrollY;
            if (std::abs(diff) < 0.002f) {
                self->m_currentScrollY = self->m_targetScrollY;
                self->m_scrollOffset = (int)std::round(self->m_currentScrollY);
                self->m_scrollAnimating = false;
                KillTimer(hWnd, 2);
                timeEndPeriod(1);
            } else {
                float factor = 1.0f - std::exp(-16.0f * dt);
                self->m_currentScrollY += diff * factor;
                self->m_scrollOffset = (int)std::round(self->m_currentScrollY);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(hWnd);
        if (self && !self->m_filteredPlayers.empty()) {
            int y = (short)HIWORD(lParam);
            if (y >= 30) {
                int clickedRow = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 34.0f);
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
                int clickedRow = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 34.0f);
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
                int clickedRow = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 34.0f);
                if (clickedRow >= 0 && clickedRow < (int)self->m_filteredPlayers.size()) {
                    self->m_selectedRow = clickedRow;
                    InvalidateRect(hWnd, nullptr, FALSE);

                    POINT pt;
                    GetCursorPos(&pt);

                    const auto& p = self->m_filteredPlayers[self->m_selectedRow];

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, IDM_CTX_OPEN_PROFILE, L"Open Roblox Profile\t(Double click)");
                    AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_PROFILE_URL, L"Copy Profile Link");
                    AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_USERNAME, L"Copy Username");
                    if (!p.display_name.empty() && p.display_name != p.username) {
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_DISPLAY_NAME, (L"Copy Display Name (" + p.display_name + L")").c_str());
                    }
                    if (p.user_id > 0) {
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_USER_ID, (L"Copy User ID (" + std::to_wstring(p.user_id) + L")").c_str());
                    }
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    if (!self->m_serverDeepLink.empty()) {
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_SERVER_DEEP_LINK, L"Copy Server Deep Link (roblox://)");
                    }
                    if (p.user_id > 0) {
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_USER_DEEP_LINK, L"Copy Friend Join Link (roblox://)");
                    }
                    AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_FULL_INFO, L"Copy Full Details");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hMenu, MF_STRING, IDM_CTX_REMOVE_PLAYER, L"Remove from list (Player left)");

                    int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
                    DestroyMenu(hMenu);

                    if (cmd == IDM_CTX_OPEN_PROFILE) {
                        self->OpenSelectedProfile();
                    } else if (cmd == IDM_CTX_COPY_PROFILE_URL) {
                        std::wstring profileUrl = (p.user_id > 0)
                            ? (L"https://www.roblox.com/users/" + std::to_wstring(p.user_id) + L"/profile")
                            : (L"https://www.roblox.com/search/users?keyword=" + p.username);
                        Utils::CopyTextToClipboard(self->m_hWnd, profileUrl);
                        self->ShowStatusMessage(L"\u2713 Copied profile link!");
                    } else if (cmd == IDM_CTX_COPY_USERNAME) {
                        Utils::CopyTextToClipboard(self->m_hWnd, p.username);
                        self->ShowStatusMessage(L"\u2713 Copied '" + p.username + L"'!");
                    } else if (cmd == IDM_CTX_COPY_DISPLAY_NAME) {
                        Utils::CopyTextToClipboard(self->m_hWnd, p.display_name);
                        self->ShowStatusMessage(L"\u2713 Copied '" + p.display_name + L"'!");
                    } else if (cmd == IDM_CTX_COPY_USER_ID) {
                        Utils::CopyTextToClipboard(self->m_hWnd, std::to_wstring(p.user_id));
                        self->ShowStatusMessage(L"\u2713 Copied User ID " + std::to_wstring(p.user_id) + L"!");
                    } else if (cmd == IDM_CTX_COPY_SERVER_DEEP_LINK) {
                        Utils::CopyTextToClipboard(self->m_hWnd, self->m_serverDeepLink);
                        self->ShowStatusMessage(L"\u2713 Copied Server Deep Link!");
                    } else if (cmd == IDM_CTX_COPY_USER_DEEP_LINK) {
                        std::wstring joinLink = L"roblox://experiences/start?userId=" + std::to_wstring(p.user_id);
                        Utils::CopyTextToClipboard(self->m_hWnd, joinLink);
                        self->ShowStatusMessage(L"\u2713 Copied Friend Join Link!");
                    } else if (cmd == IDM_CTX_COPY_FULL_INFO) {
                        std::wstring info = p.username;
                        if (!p.display_name.empty() && p.display_name != p.username) {
                            info += L" (" + p.display_name + L")";
                        }
                        if (p.user_id > 0) {
                            info += L" [ID: " + std::to_wstring(p.user_id) + L"]";
                        }
                        info += L" (" + p.status + (p.clan.empty() ? L"" : L" / " + p.clan) + L") seen at " + p.last_seen_time;
                        Utils::CopyTextToClipboard(self->m_hWnd, info);
                        self->ShowStatusMessage(L"\u2713 Copied full player details!");
                    } else if (cmd == IDM_CTX_REMOVE_PLAYER) {
                        std::wstring removedUser = p.username;
                        if (self->m_scanner) {
                            self->m_scanner->RemoveDetectedPlayer(removedUser);
                        }
                        for (auto it = self->m_allPlayers.begin(); it != self->m_allPlayers.end(); ++it) {
                            if (_wcsicmp(it->username.c_str(), removedUser.c_str()) == 0) {
                                self->m_allPlayers.erase(it);
                                break;
                            }
                        }
                        self->ApplyFilter();
                        self->ShowStatusMessage(L"\u2713 Removed " + removedUser + L" from list");
                    }
                }
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);

        if (self && !self->m_filteredPlayers.empty()) {
            int y = (short)HIWORD(lParam);
            int newHover = -1;
            if (y >= 30) {
                int rowIdx = (int)std::floor(self->m_currentScrollY + (float)(y - 30) / 34.0f);
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

            g.Clear(cardBg);

            // header bar
            Gdiplus::SolidBrush hBrush(headerBg);
            g.FillRectangle(&hBrush, 0.0f, 0.0f, (float)w, 30.0f);

            Gdiplus::Pen sepPen(borderCol, 1.0f);
            g.DrawLine(&sepPen, 0.0f, 30.0f, (float)w, 30.0f);

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font headerFont(&ff, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush headerTxtBrush(textSec);

            Gdiplus::StringFormat sfNear;
            sfNear.SetAlignment(Gdiplus::StringAlignmentNear);
            sfNear.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            sfNear.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            sfNear.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

            float colX_Seen   = std::max(400.0f, (float)(w - 108));
            float colX_Clan   = std::max(275.0f, colX_Seen - 125.0f);
            float colX_Status = std::max(180.0f, colX_Clan - 95.0f);
            float colX_Player = 16.0f;
            float colW_Player = colX_Status - colX_Player - 10.0f;

            g.DrawString(L"PLAYER", -1, &headerFont, Gdiplus::RectF(colX_Player, 0.0f, colW_Player, 30.0f), &sfNear, &headerTxtBrush);
            g.DrawString(L"STATUS", -1, &headerFont, Gdiplus::RectF(colX_Status, 0.0f, 85.0f, 30.0f), &sfNear, &headerTxtBrush);
            g.DrawString(L"CLAN",   -1, &headerFont, Gdiplus::RectF(colX_Clan,   0.0f, (colX_Seen - colX_Clan - 10.0f), 30.0f), &sfNear, &headerTxtBrush);
            g.DrawString(L"SEEN",   -1, &headerFont, Gdiplus::RectF(colX_Seen,   0.0f, (float)(w - colX_Seen - 10), 30.0f), &sfNear, &headerTxtBrush);

            // rows
            if (!self || self->m_filteredPlayers.empty()) {
                Gdiplus::Font emptyFont(&ff, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush emptyBrush(textSec);
                Gdiplus::StringFormat sfCenter;
                sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
                sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                std::wstring emptyMsg = (self && self->m_allPlayers.empty())
                    ? L"No players detected yet in this session."
                    : L"No players match the current search / filter.";

                g.DrawString(emptyMsg.c_str(), -1, &emptyFont,
                                Gdiplus::RectF(0.0f, 30.0f, (float)w, (float)(h - 30)), &sfCenter, &emptyBrush);
            } else {
                int rowH = 34;
                int visibleRows = std::max(1, (h - 30) / rowH);
                int maxOffset = std::max(0, (int)self->m_filteredPlayers.size() - visibleRows);
                self->m_currentScrollY = std::clamp(self->m_currentScrollY, 0.0f, (float)maxOffset);

                // clip rows area
                g.SetClip(Gdiplus::RectF(0.0f, 30.0f, (float)w, (float)(h - 30)));

                Gdiplus::Font fontUser(&ff, 9.2f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fontClan(&ff, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fontSeen(&ff, 8.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::Font fontBadge(&ff, 7.8f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);

                Gdiplus::SolidBrush userBrush(textPrim);
                Gdiplus::SolidBrush clanBrush(textSec);
                Gdiplus::SolidBrush seenBrush(textSec);
                Gdiplus::Pen rowLinePen(lineCol, 1.0f);

                Gdiplus::StringFormat sfBadge;
                sfBadge.SetAlignment(Gdiplus::StringAlignmentCenter);
                sfBadge.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                int startIdx = (int)std::floor(self->m_currentScrollY);
                int endIdx = std::min((int)self->m_filteredPlayers.size(), startIdx + visibleRows + 2);
                for (int i = startIdx; i < endIdx; ++i) {
                    float curY = 30.0f + ((float)i - self->m_currentScrollY) * (float)rowH;
                    const auto& p = self->m_filteredPlayers[i];

                    // row background
                    bool isSelected = (self->m_selectedRow == i);
                    bool isHovered  = (self->m_hoveredRow == i);
                    bool isEven     = (i % 2 == 0);

                    Gdiplus::Color curRowBg = isSelected ? selBg : (isHovered ? hoverBg : (isEven ? cardBg : altRowBg));
                    Gdiplus::SolidBrush rBrush(curRowBg);
                    g.FillRectangle(&rBrush, 0.0f, curY, (float)w, (float)rowH);

                    // row selection accent
                    if (isSelected) {
                        Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 85, 117, 239));
                        g.FillRectangle(&barBrush, 0.0f, curY, 3.5f, (float)rowH);
                    }

                    // row bottom divider line
                    g.DrawLine(&rowLinePen, 0.0f, curY + rowH, (float)w, curY + rowH);

                    // avatar circle
                    float avSize = 24.0f;
                    float avX = colX_Player;
                    float avY = curY + (rowH - avSize) / 2.0f;
                    Utils::DrawAvatarCircle(g, p.avatar_png, p.username, p.status, avX, avY, avSize, isDark, false);

                    // user and display name
                    std::wstring displayLabel = p.username;
                    if (!p.display_name.empty() && p.display_name != p.username) {
                        displayLabel = p.username + L" (" + p.display_name + L")";
                    }
                    float textStartX = colX_Player + avSize + 9.0f;
                    float textAvailW = std::max(40.0f, colW_Player - avSize - 9.0f);
                    g.DrawString(displayLabel.c_str(), -1, &fontUser, Gdiplus::RectF(textStartX, curY, textAvailW, (float)rowH), &sfNear, &userBrush);

                    // status badge
                    bool isEnemy = (p.status == L"Enemy");
                    std::wstring statusLabel = isEnemy ? L"\u25CF ENEMY" : L"\u25CF ALLY";
                    Gdiplus::Color badgeBg = isEnemy ? (isDark ? Gdiplus::Color(255, 55, 26, 32) : Gdiplus::Color(255, 254, 240, 242))
                                                     : (isDark ? Gdiplus::Color(255, 25, 36, 30) : Gdiplus::Color(255, 243, 248, 245));
                    Gdiplus::Color badgeTxt = isEnemy
                        ? (isDark ? Gdiplus::Color(255, 255, 125, 130) : Gdiplus::Color(255, 195, 45, 55))
                        : (isDark ? Gdiplus::Color(255, 120, 200, 155) : Gdiplus::Color(255, 52, 126, 85));
                    Gdiplus::Color badgeBorder = isEnemy
                        ? (isDark ? Gdiplus::Color(255, 75, 40, 48) : Gdiplus::Color(255, 248, 215, 220))
                        : (isDark ? Gdiplus::Color(255, 42, 62, 50) : Gdiplus::Color(255, 220, 234, 225));

                    float bW = 68.0f;
                    float bH = 19.0f;
                    float bY = curY + (rowH - bH) / 2.0f;

                    Gdiplus::GraphicsPath bp;
                    Utils::AddRoundedRect(bp, Gdiplus::RectF(colX_Status, bY, bW, bH), 5.5f);
                    Gdiplus::SolidBrush bBrush(badgeBg);
                    Gdiplus::Pen bPen(badgeBorder, 1.0f);
                    g.FillPath(&bBrush, &bp);
                    g.DrawPath(&bPen, &bp);

                    Gdiplus::SolidBrush bTxtBrush(badgeTxt);
                    g.DrawString(statusLabel.c_str(), -1, &fontBadge, Gdiplus::RectF(colX_Status, bY, bW, bH), &sfBadge, &bTxtBrush);

                    // clan badge
                    if (!p.clan.empty()) {
                        Gdiplus::RectF cBounds;
                        g.MeasureString(p.clan.c_str(), -1, &fontClan, Gdiplus::PointF(0, 0), &cBounds);
                        float maxClanW = std::max(40.0f, colX_Seen - colX_Clan - 12.0f);
                        float clanPillW = std::min(maxClanW, cBounds.Width + 12.0f);
                        Gdiplus::RectF clanRect(colX_Clan, bY, clanPillW, bH);
                        Gdiplus::GraphicsPath clanPath;
                        Utils::AddRoundedRect(clanPath, clanRect, 5.5f);
                        Gdiplus::SolidBrush cBgBrush(isDark ? Gdiplus::Color(255, 40, 47, 64) : Gdiplus::Color(255, 236, 240, 248));
                        g.FillPath(&cBgBrush, &clanPath);
                        g.DrawString(p.clan.c_str(), -1, &fontClan, clanRect, &sfBadge, &clanBrush);
                    } else {
                        g.DrawString(L"\u2014", -1, &fontSeen, Gdiplus::RectF(colX_Clan + 4.0f, curY, 30.0f, (float)rowH), &sfNear, &clanBrush);
                    }

                    // seen time
                    g.DrawString(p.last_seen_time.c_str(), -1, &fontSeen, Gdiplus::RectF(colX_Seen, curY, (float)(w - colX_Seen - 10), (float)rowH), &sfNear, &seenBrush);
                }

                g.ResetClip();

                // scrollbar
                if ((int)self->m_filteredPlayers.size() > visibleRows && maxOffset > 0) {
                    float trackX = (float)(w - 7);
                    float trackY = 34.0f;
                    float trackW = 4.0f;
                    float trackH = (float)(h - 38);

                    float thumbH = std::max(16.0f, ((float)visibleRows / (float)self->m_filteredPlayers.size()) * trackH);
                    float thumbY = trackY + (self->m_currentScrollY / (float)maxOffset) * (trackH - thumbH);

                    Gdiplus::GraphicsPath thumbPath;
                    Utils::AddRoundedRect(thumbPath, Gdiplus::RectF(trackX, thumbY, trackW, thumbH), 2.0f);

                    Gdiplus::Color thumbCol = isDark ? Gdiplus::Color(140, 100, 115, 145) : Gdiplus::Color(160, 180, 190, 205);
                    Gdiplus::SolidBrush thumbBrush(thumbCol);
                    g.FillPath(&thumbBrush, &thumbPath);
                }
            }

            // card border
            Gdiplus::GraphicsPath cardBorderPath;
            Utils::AddRoundedRect(cardBorderPath, Gdiplus::RectF(0.5f, 0.5f, (float)(w - 1), (float)(h - 1)), 8.0f);
            Gdiplus::Pen outerBorderPen(borderCol, 1.0f);
            g.DrawPath(&outerBorderPen, &cardBorderPath);
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

LRESULT CALLBACK DetectedWindow::SearchPillWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DetectedWindow* self = reinterpret_cast<DetectedWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
        return TRUE;
    case WM_LBUTTONDOWN:
        if (self && self->m_hSearchEdit && IsWindow(self->m_hSearchEdit)) {
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
        COLORREF bg = isDark ? RGB(34, 39, 52) : RGB(255, 255, 255);
        COLORREF text = isDark ? RGB(241, 243, 250) : RGB(36, 41, 56);
        SetBkColor(hdc, bg);
        SetTextColor(hdc, text);
        static HBRUSH hDarkPillBg = CreateSolidBrush(RGB(34, 39, 52));
        static HBRUSH hLightPillBg = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hDarkPillBg : hLightPillBg);
    }
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (self && self->m_hSearchEdit && IsWindow(self->m_hSearchEdit)) {
            SetWindowPos(self->m_hSearchEdit, nullptr, 27, 4, std::max(10, w - 39), h - 8, SWP_NOZORDER);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
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

            bool isDark = (self && self->m_darkMode);
            Gdiplus::Color windowBg = isDark ? Gdiplus::Color(255, 23, 27, 36) : Gdiplus::Color(255, 245, 247, 252);
            g.Clear(windowBg);

            bool isFocused = (self && self->m_hSearchEdit && GetFocus() == self->m_hSearchEdit);

            Gdiplus::Color pillBg = isDark ? Gdiplus::Color(255, 34, 39, 52) : Gdiplus::Color(255, 255, 255, 255);
            Gdiplus::Color pillBorder = isFocused
                ? Gdiplus::Color(255, 85, 117, 239)
                : (isDark ? Gdiplus::Color(255, 58, 68, 88) : Gdiplus::Color(255, 215, 222, 235));

            float radius = (float)(h - 1) / 2.0f;
            Gdiplus::GraphicsPath pillPath;
            Utils::AddRoundedRect(pillPath, Gdiplus::RectF(0.5f, 0.5f, (float)(w - 1), (float)(h - 1)), radius);

            Gdiplus::SolidBrush bgBrush(pillBg);
            g.FillPath(&bgBrush, &pillPath);

            Gdiplus::Pen borderPen(pillBorder, isFocused ? 1.5f : 1.0f);
            g.DrawPath(&borderPen, &pillPath);

            // Search Icon (magnifying glass)
            Gdiplus::Color iconCol = isFocused
                ? Gdiplus::Color(255, 85, 117, 239)
                : (isDark ? Gdiplus::Color(255, 126, 139, 159) : Gdiplus::Color(255, 150, 160, 175));
            Gdiplus::Pen iconPen(iconCol, 1.4f);

            float centerY = (float)h / 2.0f;
            g.DrawEllipse(&iconPen, 10.0f, centerY - 5.0f, 7.5f, 7.5f);
            g.DrawLine(&iconPen, 16.0f, centerY + 1.5f, 19.5f, centerY + 5.0f);
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

void DetectedWindow::ApplyTheme(const Config& config) {
    m_darkMode = config.appearance.dark_mode;
    if (m_hWnd && IsWindow(m_hWnd)) {
        RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void DetectedWindow::UpdateData(const std::vector<DetectedPlayer>& players, const std::wstring& summary, const Config& config, const std::wstring& deepLink) {
    if (m_darkMode != config.appearance.dark_mode) {
        ApplyTheme(config);
    }

    if (!deepLink.empty()) {
        m_serverDeepLink = deepLink;
    }

    // compare prev and new player list
    bool playersChanged = (m_allPlayers.size() != players.size());
    if (!playersChanged) {
        for (size_t i = 0; i < players.size(); ++i) {
            if (m_allPlayers[i].username != players[i].username ||
                m_allPlayers[i].display_name != players[i].display_name ||
                m_allPlayers[i].status != players[i].status ||
                m_allPlayers[i].clan != players[i].clan ||
                m_allPlayers[i].last_seen_time != players[i].last_seen_time ||
                m_allPlayers[i].avatar_png.size() != players[i].avatar_png.size()) {
                playersChanged = true;
                break;
            }
        }
    }

    if (playersChanged) {
        m_allPlayers = players;

        int enemiesCount = 0, alliesCount = 0;
        for (const auto& p : m_allPlayers) {
            if (p.status == L"Enemy") enemiesCount++;
            else alliesCount++;
        }

        if (m_hFilterAllBtn && IsWindow(m_hFilterAllBtn)) {
            SetWindowTextW(m_hFilterAllBtn, (L"All (" + std::to_wstring(m_allPlayers.size()) + L")").c_str());
        }
        if (m_hFilterEnemiesBtn && IsWindow(m_hFilterEnemiesBtn)) {
            SetWindowTextW(m_hFilterEnemiesBtn, (L"Enemies (" + std::to_wstring(enemiesCount) + L")").c_str());
        }
        if (m_hFilterAlliesBtn && IsWindow(m_hFilterAlliesBtn)) {
            SetWindowTextW(m_hFilterAlliesBtn, (L"Allies (" + std::to_wstring(alliesCount) + L")").c_str());
        }

        ApplyFilter();
    }

    // only update summary text if text changed
    if (m_summaryText != summary) {
        m_summaryText = summary;
        if (m_hWnd && IsWindow(m_hWnd)) {
            RECT rc;
            GetClientRect(m_hWnd, &rc);
            RECT rcHeader = { 50, 28, rc.right - 44, 58 };
            InvalidateRect(m_hWnd, &rcHeader, FALSE);
        }
    }
}

void DetectedWindow::Show(const std::vector<DetectedPlayer>& players, const std::wstring& summary, const Config& config, const std::wstring& deepLink) {
    if (!m_hWnd || !IsWindow(m_hWnd)) {
        // reg main window class
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = DETECTED_CLASS_NAME;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);

        // reg custom table class
        WNDCLASSEXW twc = { sizeof(WNDCLASSEXW) };
        twc.lpfnWndProc = TableWndProc;
        twc.hInstance = m_hInstance;
        twc.lpszClassName = TABLE_CLASS_NAME;
        twc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        twc.hbrBackground = nullptr;
        RegisterClassExW(&twc);

        // register pill search class
        WNDCLASSEXW swc = { sizeof(WNDCLASSEXW) };
        swc.lpfnWndProc = SearchPillWndProc;
        swc.hInstance = m_hInstance;
        swc.lpszClassName = PILL_CLASS_NAME;
        swc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
        swc.hbrBackground = nullptr;
        RegisterClassExW(&swc);

        m_hWnd = CreateWindowExW(
            0,
            DETECTED_CLASS_NAME,
            L"Detected players \u2014 BorderWatch",
            WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 580, 520,
            m_hParent, nullptr, m_hInstance, this
        );

        // apply corners and shadow
        int cornerPref = DWMWCP_ROUND;
        DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
        MARGINS shadowMargins = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(m_hWnd, &shadowMargins);
        SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        // top right window controls
        m_hTopCloseBtn = CreateWindowExW(
            0, L"BUTTON", L"\u00D7",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            580 - 42, 14, 28, 28,
            m_hWnd, (HMENU)IDC_BTN_DET_TOP_CLOSE, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hTopCloseBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        if (!m_hFontRegular) {
            m_hFontRegular = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
        if (!m_hFontBold) {
            m_hFontBold = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }

        m_summaryText = summary;

        // second row:
        m_hFilterAllBtn = CreateWindowExW(
            0, L"BUTTON", L"All",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            18, 62, 68, 27,
            m_hWnd, (HMENU)IDC_FILTER_ALL, m_hInstance, nullptr
        );
        s_oldBtnProc = (WNDPROC)SetWindowLongPtrW(m_hFilterAllBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        m_hFilterEnemiesBtn = CreateWindowExW(
            0, L"BUTTON", L"Enemies",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            90, 62, 92, 27,
            m_hWnd, (HMENU)IDC_FILTER_ENEMIES, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hFilterEnemiesBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        m_hFilterAlliesBtn = CreateWindowExW(
            0, L"BUTTON", L"Allies",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            186, 62, 78, 27,
            m_hWnd, (HMENU)IDC_FILTER_ALLIES, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hFilterAlliesBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        // search pill container and input box
        m_hSearchContainer = CreateWindowExW(
            0, PILL_CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE,
            270, 62, 292, 27,
            m_hWnd, nullptr, m_hInstance, this
        );

        m_hSearchEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            27, 4, 257, 19,
            m_hSearchContainer, (HMENU)IDC_EDT_SEARCH, m_hInstance, nullptr
        );
        SendMessageW(m_hSearchEdit, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
        SendMessageW(m_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search player...");

        // table
        m_hTable = CreateWindowExW(
            0, TABLE_CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE,
            18, 96, 544, 370,
            m_hWnd, (HMENU)102, m_hInstance, this
        );

        // bottom action bar
        m_hProfileBtn = CreateWindowExW(
            0, L"BUTTON", L"Open Profile",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            18, 476, 105, 32,
            m_hWnd, (HMENU)IDC_BTN_PROFILE, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hProfileBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        m_hCopyBtn = CreateWindowExW(
            0, L"BUTTON", L"Copy Name",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            130, 476, 100, 32,
            m_hWnd, (HMENU)IDC_BTN_COPY, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hCopyBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        m_hDeepLinkBtn = CreateWindowExW(
            0, L"BUTTON", L"Deep Link",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            238, 476, 100, 32,
            m_hWnd, (HMENU)IDC_BTN_DEEP_LINK, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hDeepLinkBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        m_hClearBtn = CreateWindowExW(
            0, L"BUTTON", L"Clear Players",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0,
            m_hWnd, (HMENU)IDC_BTN_CLEAR_PLAYERS, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hClearBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        m_hRefreshBtn = CreateWindowExW(
            0, L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            467, 476, 95, 32,
            m_hWnd, (HMENU)IDC_BTN_REFRESH, m_hInstance, nullptr
        );
        SetWindowLongPtrW(m_hRefreshBtn, GWLP_WNDPROC, (LONG_PTR)HandCursorBtnSubclass);

        // tooltip for buttons
        m_hTooltip = CreateWindowExW(
            WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            m_hWnd, nullptr, m_hInstance, nullptr
        );
        if (m_hTooltip) {
            SetWindowPos(m_hTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SendMessageW(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, 340);
            TOOLINFOW ti = { sizeof(TOOLINFOW) };
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = m_hWnd;

            ti.uId = (UINT_PTR)m_hClearBtn;
            ti.lpszText = const_cast<LPWSTR>(L"Clear all detected players from the current session.");
            SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            ti.uId = (UINT_PTR)m_hDeepLinkBtn;
            ti.lpszText = const_cast<LPWSTR>(L"Copy roblox:// server deep link to clipboard for sharing with clan.");
            SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

            TOOLINFOW tiBadge = { sizeof(TOOLINFOW) };
            tiBadge.uFlags = TTF_SUBCLASS;
            tiBadge.hwnd = m_hWnd;
            tiBadge.uId = 2099;
            tiBadge.rect = { 16, 12, 56, 52 };
            tiBadge.lpszText = const_cast<LPWSTR>(L"Click [B] to open full Watchlist Database table (all players & sorting).");
            SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&tiBadge);
        }
    }

    RECT rc = { 0 };
    GetClientRect(m_hWnd, &rc);
    RelayoutControls(rc.right - rc.left, rc.bottom - rc.top);

    UpdateData(players, summary, config, deepLink);
    ShowWindow(m_hWnd, SW_SHOW);
    SetForegroundWindow(m_hWnd);
}

void DetectedWindow::Hide() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

bool DetectedWindow::IsVisible() const {
    return m_hWnd && IsWindow(m_hWnd) && IsWindowVisible(m_hWnd);
}

void DetectedWindow::RelayoutControls(int w, int h) {
    if (m_hTopCloseBtn && IsWindow(m_hTopCloseBtn)) {
        SetWindowPos(m_hTopCloseBtn, nullptr, w - 42, 14, 28, 28, SWP_NOZORDER);
    }
    if (m_hSearchContainer && IsWindow(m_hSearchContainer)) {
        SetWindowPos(m_hSearchContainer, nullptr, 270, 62, std::max(60, w - 18 - 270), 27, SWP_NOZORDER);
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
    if (m_hCopyBtn && IsWindow(m_hCopyBtn)) {
        SetWindowPos(m_hCopyBtn, nullptr, 136, btnY, 95, 32, SWP_NOZORDER);
    }
    if (m_hDeepLinkBtn && IsWindow(m_hDeepLinkBtn)) {
        SetWindowPos(m_hDeepLinkBtn, nullptr, 239, btnY, 105, 32, SWP_NOZORDER);
    }
    int refreshW = 85;
    int clearW = 110;
    int refreshX = w - 18 - refreshW;
    int clearX = refreshX - 10 - clearW;
    if (m_hClearBtn && IsWindow(m_hClearBtn)) {
        SetWindowPos(m_hClearBtn, nullptr, clearX, btnY, clearW, 32, SWP_NOZORDER);
    }
    if (m_hRefreshBtn && IsWindow(m_hRefreshBtn)) {
        SetWindowPos(m_hRefreshBtn, nullptr, refreshX, btnY, refreshW, 32, SWP_NOZORDER);
    }
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

LRESULT CALLBACK DetectedWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DetectedWindow* self = reinterpret_cast<DetectedWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_NCCALCSIZE: {
        if (wParam == TRUE) {
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (self && w > 0 && h > 0) {
            self->RelayoutControls(w, h);
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 550;
        mmi->ptMinTrackSize.y = 380;
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        // resizing edges
        const int b = 8;
        bool left   = (pt.x < b);
        bool right  = (pt.x >= clientW - b);
        bool top    = (pt.y < b);
        bool bottom = (pt.y >= clientH - b);

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        // allow top right close button to receive normal clicks
        if (pt.y >= 10 && pt.y <= 48 && pt.x >= clientW - 48) {
            return HTCLIENT;
        }

        // allow [B] badge icon to receive normal clicks
        if (pt.y >= 12 && pt.y <= 52 && pt.x >= 16 && pt.x <= 56) {
            return HTCLIENT;
        }

        // dragging
        if (pt.y <= 58) {
            return HTCAPTION;
        }

        return HTCLIENT;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT && self) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            if (pt.y >= 12 && pt.y <= 52 && pt.x >= 16 && pt.x <= 56) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        if (y >= 12 && y <= 52 && x >= 16 && x <= 56) {
            if (self && self->m_hParent) {
                SendMessageW(self->m_hParent, WM_COMMAND, MAKEWPARAM(2099, 0), 0);
            }
            return 0;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdcScreen, w, h);
        HGDIOBJ hOldBm = SelectObject(hdcMem, hbmMem);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            bool isDark = (self && self->m_darkMode);
            Gdiplus::Color bgColor = isDark ? Gdiplus::Color(255, 20, 24, 33) : Gdiplus::Color(255, 244, 247, 252);
            Gdiplus::Color titleColor = isDark ? Gdiplus::Color(255, 243, 245, 252) : Gdiplus::Color(255, 32, 37, 52);
            Gdiplus::Color subColor = isDark ? Gdiplus::Color(255, 154, 165, 188) : Gdiplus::Color(255, 126, 136, 158);
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

            // title
            Gdiplus::Font titleFont(&ff, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush titleBrush(titleColor);
            g.DrawString(L"Detected Players", -1, &titleFont, Gdiplus::PointF(60, 11), &titleBrush);

            // summary subtitle
            if (self && !self->m_summaryText.empty()) {
                Gdiplus::Font subFont(&ff, 8.8f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush subBrush(subColor);
                Gdiplus::StringFormat sfNear;
                sfNear.SetAlignment(Gdiplus::StringAlignmentNear);
                sfNear.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                sfNear.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                sfNear.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                g.DrawString(self->m_summaryText.c_str(), -1, &subFont,
                                Gdiplus::RectF(60.0f, 32.0f, (float)std::max(100, w - 115), 20.0f), &sfNear, &subBrush);
            }

            // copied / tip status pill in top right header
            if (self && !self->m_copiedText.empty()) {
                bool isTip = (self->m_copiedText.find(L"Tip:") != std::wstring::npos);
                Gdiplus::Color pBg = isTip
                    ? (isDark ? Gdiplus::Color(255, 36, 48, 78) : Gdiplus::Color(255, 230, 238, 255))
                    : (isDark ? Gdiplus::Color(255, 24, 54, 40) : Gdiplus::Color(255, 228, 250, 236));
                Gdiplus::Color pTxt = isTip
                    ? (isDark ? Gdiplus::Color(255, 150, 180, 255) : Gdiplus::Color(255, 62, 94, 214))
                    : (isDark ? Gdiplus::Color(255, 76, 206, 140) : Gdiplus::Color(255, 32, 138, 86));
                Gdiplus::Color pBorder = isTip
                    ? Gdiplus::Color(90, 85, 117, 239)
                    : Gdiplus::Color(90, 48, 164, 108);

                Gdiplus::Font pillFont(&ff, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::RectF bBounds;
                g.MeasureString(self->m_copiedText.c_str(), -1, &pillFont, Gdiplus::PointF(0, 0), &bBounds);
                float pW = std::min((float)(w - 260), bBounds.Width + 16.0f);
                float pX = (float)(w - 50) - pW;
                Gdiplus::RectF pillRect(pX, 15.0f, pW, 20.0f);
                Gdiplus::GraphicsPath pillPath;
                Utils::AddRoundedRect(pillPath, pillRect, 6.0f);
                Gdiplus::SolidBrush pBrush(pBg);
                Gdiplus::Pen pPen(pBorder, 1.0f);
                Gdiplus::SolidBrush tBrush(pTxt);
                g.FillPath(&pBrush, &pillPath);
                g.DrawPath(&pPen, &pillPath);
                g.DrawString(self->m_copiedText.c_str(), -1, &pillFont, pillRect, &sfCenter, &tBrush);
            }
        }

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOldBm);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        bool isDark = (self && self->m_darkMode);
        COLORREF bg = isDark ? RGB(20, 24, 33) : RGB(244, 247, 252);
        HBRUSH hBr = CreateSolidBrush(bg);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);
        return 1;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        bool isDark = (self && self->m_darkMode);
        COLORREF bg = isDark ? RGB(30, 35, 48) : RGB(255, 255, 255);
        COLORREF text = isDark ? RGB(243, 245, 252) : RGB(32, 37, 52);
        SetBkColor(hdc, bg);
        SetTextColor(hdc, text);
        static HBRUSH hDarkEditBg = CreateSolidBrush(RGB(30, 35, 48));
        static HBRUSH hLightEditBg = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)(isDark ? hDarkEditBg : hLightEditBg);
    }
    case WM_TIMER: {
        if (wParam == 999 && self) {
            KillTimer(hWnd, 999);
            self->m_copiedText.clear();
            RECT rc;
            GetClientRect(hWnd, &rc);
            RECT rcHeader = { 200, 8, rc.right - 44, 54 };
            InvalidateRect(hWnd, &rcHeader, FALSE);
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

            if (id == IDC_BTN_DET_TOP_CLOSE) {
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
            } else if (id == IDC_FILTER_ALL || id == IDC_FILTER_ENEMIES || id == IDC_FILTER_ALLIES) {
                // Filter Pills
                Utils::AddRoundedRect(path, rect, 7.0f);
                bool isActive = (id == IDC_FILTER_ALL && self->m_filterMode == 0) ||
                                (id == IDC_FILTER_ENEMIES && self->m_filterMode == 1) ||
                                (id == IDC_FILTER_ALLIES && self->m_filterMode == 2);

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
            } else if (id == IDC_BTN_REFRESH) {
                // primary action button
                Utils::AddRoundedRect(path, rect, 8.0f);
                bgColor = isPressed ? Gdiplus::Color(255, 66, 97, 220) : Gdiplus::Color(255, 85, 117, 239);
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else {
                // secondary action buttons
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
            bool isTopBtn = (id == IDC_BTN_DET_TOP_CLOSE);
            float fontSize = isTopBtn ? 12.5f : 9.0f;
            bool isBold = (id == IDC_BTN_REFRESH || isTopBtn);
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
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_EDT_SEARCH && code == EN_CHANGE && self) {
            wchar_t queryBuf[256] = { 0 };
            GetWindowTextW(self->m_hSearchEdit, queryBuf, 256);
            self->m_searchQuery = queryBuf;
            self->ApplyFilter();
            return 0;
        }

        if (self) {
            switch (id) {
            case IDC_BTN_DET_TOP_CLOSE:
                ShowWindow(self->m_hWnd, SW_HIDE);
                break;
            case IDC_FILTER_ALL:
                self->m_filterMode = 0;
                self->ApplyFilter();
                InvalidateRect(self->m_hFilterAllBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterEnemiesBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterAlliesBtn, nullptr, TRUE);
                break;
            case IDC_FILTER_ENEMIES:
                self->m_filterMode = 1;
                self->ApplyFilter();
                InvalidateRect(self->m_hFilterAllBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterEnemiesBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterAlliesBtn, nullptr, TRUE);
                break;
            case IDC_FILTER_ALLIES:
                self->m_filterMode = 2;
                self->ApplyFilter();
                InvalidateRect(self->m_hFilterAllBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterEnemiesBtn, nullptr, TRUE);
                InvalidateRect(self->m_hFilterAlliesBtn, nullptr, TRUE);
                break;
            case IDC_BTN_PROFILE:
                self->OpenSelectedProfile();
                break;
            case IDC_BTN_COPY:
                self->CopySelectedUsername();
                break;
            case IDC_BTN_DEEP_LINK:
                if (!self->m_serverDeepLink.empty()) {
                    Utils::CopyTextToClipboard(self->m_hWnd, self->m_serverDeepLink);
                    self->ShowStatusMessage(L"\u2713 Copied Server Deep Link!");
                } else {
                    self->ShowStatusMessage(L"\u26A0 No active server link available");
                }
                break;
            case IDC_BTN_CLEAR_PLAYERS:
                if (self->m_scanner) {
                    self->m_scanner->ClearDetectedPlayers();
                }
                self->m_allPlayers.clear();
                self->m_filteredPlayers.clear();
                self->m_selectedRow = -1;
                self->m_summaryText = L"0 enemies / 0 allies detected in this session's log. Cleared.";
                InvalidateRect(self->m_hTable, nullptr, FALSE);
                InvalidateRect(self->m_hWnd, nullptr, FALSE);
                self->ShowStatusMessage(L"\u2713 Cleared session players list");
                break;
            case IDC_BTN_REFRESH:
                SendMessageW(self->m_hParent, WM_COMMAND, MAKEWPARAM(2001, 0), 0);
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
