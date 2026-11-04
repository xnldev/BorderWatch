#include "../include/ToastManager.h"
#include "../include/LogScanner.h"
#include "../include/Utils.h"
#include <windowsx.h>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

static const wchar_t* TOAST_CLASS_NAME = L"BorderWatchToastClass";
static constexpr int kToastWindowHeight = 98;
static constexpr float kToastMargin = 11.0f;

ToastManager::ToastManager(HINSTANCE hInstance) : m_hInstance(hInstance) {}

ToastManager::~ToastManager() {
    CloseAll();
}

void ToastManager::Initialize() {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = ToastWndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = TOAST_CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassExW(&wc);
}

void ToastManager::CloseAll() {
    for (auto& t : m_toasts) {
        if (t->hWnd && IsWindow(t->hWnd)) {
            DestroyWindow(t->hWnd);
        }
    }
    m_toasts.clear();
}

ToastCard* ToastManager::FindToastByHwnd(HWND hWnd) {
    for (auto& t : m_toasts) {
        if (t->hWnd == hWnd) return t.get();
    }
    return nullptr;
}

LRESULT CALLBACK ToastManager::ToastWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ToastManager* self = reinterpret_cast<ToastManager*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ToastManager*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        return self->HandleToastMessage(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT ToastManager::HandleToastMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;

    case WM_MOUSEMOVE: {
        ToastCard* toast = FindToastByHwnd(hWnd);
        if (!toast || toast->closing) return 0;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (!toast->is_hovered) {
            toast->is_hovered = true;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }

        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        float cardRight = (float)w - kToastMargin;
        float closeX = cardRight - 30.0f;
        float closeY = kToastMargin + 8.0f;

        bool overClose = (x >= (int)(closeX - 4) && x <= (int)(closeX + 24) &&
                          y >= (int)(closeY - 4) && y <= (int)(closeY + 24));

        if (toast->close_hovered != overClose) {
            toast->close_hovered = overClose;
        }
        RenderToast(*toast, m_lastConfig);
        return 0;
    }

    case WM_MOUSELEAVE: {
        ToastCard* toast = FindToastByHwnd(hWnd);
        if (toast) {
            toast->is_hovered = false;
            toast->close_hovered = false;
            toast->last_tick_time = std::chrono::steady_clock::now();
            RenderToast(*toast, m_lastConfig);
        }
        return 0;
    }

    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        ToastCard* toast = FindToastByHwnd(hWnd);
        if (toast) {
            toast->closing = true;
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        ToastCard* toast = FindToastByHwnd(hWnd);
        if (!toast) return 0;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        float cardRight = (float)w - kToastMargin;
        float closeX = cardRight - 30.0f;
        float closeY = kToastMargin + 8.0f;

        bool clickedClose = (x >= (int)(closeX - 4) && x <= (int)(closeX + 24) &&
                             y >= (int)(closeY - 4) && y <= (int)(closeY + 24));

        toast->closing = true;
        if (!clickedClose && m_hParentWnd && IsWindow(m_hParentWnd)) {
            PostMessageW(m_hParentWnd, WM_BW_OPEN_FROM_TOAST, 0, 0);
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ToastManager::RenderToast(ToastCard& toast, const Config& config) {
    if (!toast.hWnd || !IsWindow(toast.hWnd)) return;

    int width = config.notification.width + (int)(kToastMargin * 2.0f);
    int height = kToastWindowHeight;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HGDIOBJ hOldBitmap = SelectObject(hdcMem, hBitmap);

    {
        Gdiplus::Graphics g(hdcMem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        bool isDark = config.appearance.dark_mode;

        Gdiplus::Color bgColor = isDark ? Gdiplus::Color(255, 28, 33, 45) : Gdiplus::Color(255, 252, 253, 255);
        Gdiplus::Color textColor = isDark ? Gdiplus::Color(255, 243, 246, 252) : Gdiplus::Color(255, 24, 29, 40);

        if (_wcsicmp(config.notification.background.c_str(), L"#F5F5F7") != 0) {
            bgColor = Utils::HexToColor(config.notification.background);
        }
        if (_wcsicmp(config.notification.text.c_str(), L"#1D1D1F") != 0) {
            textColor = Utils::HexToColor(config.notification.text);
        }

        bool isEnemy = (toast.status == L"Enemy");
        bool isSystemToast = (toast.username == L"Server Link Copied" || toast.username == L"Web Link Copied");

        Gdiplus::Color accentColor = isEnemy
            ? Utils::HexToColor(config.notification.enemy_accent)
            : (isSystemToast ? Gdiplus::Color(255, 85, 117, 239) : Utils::HexToColor(config.notification.ally_accent));

        Gdiplus::RectF cardRect(kToastMargin, kToastMargin,
                                (float)width - 2.0f * kToastMargin,
                                (float)height - 2.0f * kToastMargin);

        for (int i = 3; i >= 1; i--) {
            Gdiplus::GraphicsPath shadowPath;
            Gdiplus::RectF sRect(cardRect.X - i * 0.8f, cardRect.Y - i * 0.4f + 2.5f,
                                 cardRect.Width + i * 1.6f, cardRect.Height + i * 1.2f);
            Utils::AddRoundedRect(shadowPath, sRect, 16.0f);
            BYTE shadowAlpha = (BYTE)((isDark ? 24 : 14) * (4 - i));
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(shadowAlpha, 0, 0, 0));
            g.FillPath(&shadowBrush, &shadowPath);
        }

        Gdiplus::GraphicsPath cardPath;
        Utils::AddRoundedRect(cardPath, cardRect, 15.0f);

        BYTE cardAlpha = (BYTE)std::clamp(config.notification.opacity * 255.0, 100.0, 255.0);
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(cardAlpha, bgColor.GetR(), bgColor.GetG(), bgColor.GetB()));
        g.FillPath(&bgBrush, &cardPath);

        g.SetClip(&cardPath, Gdiplus::CombineModeReplace);

        Gdiplus::LinearGradientBrush tintBrush(
            Gdiplus::PointF(cardRect.X, cardRect.Y),
            Gdiplus::PointF(cardRect.X + 115.0f, cardRect.Y),
            Gdiplus::Color(isDark ? 36 : 22, accentColor.GetR(), accentColor.GetG(), accentColor.GetB()),
            Gdiplus::Color(0, accentColor.GetR(), accentColor.GetG(), accentColor.GetB())
        );
        g.FillRectangle(&tintBrush, cardRect.X, cardRect.Y, 115.0f, cardRect.Height);

        Gdiplus::GraphicsPath accentBarPath;
        Gdiplus::RectF barRect(cardRect.X + 10.0f, cardRect.Y + 14.0f, 4.0f, cardRect.Height - 28.0f);
        Utils::AddRoundedRect(accentBarPath, barRect, 2.0f);
        Gdiplus::SolidBrush accentBrush(accentColor);
        g.FillPath(&accentBrush, &accentBarPath);

        double frac = (toast.total_duration_ms > 0.0)
            ? std::clamp(toast.remaining_ms / toast.total_duration_ms, 0.0, 1.0)
            : 0.0;
        float progHeight = 3.0f;
        Gdiplus::SolidBrush progTrackBrush(Gdiplus::Color(isDark ? 35 : 22, accentColor.GetR(), accentColor.GetG(), accentColor.GetB()));
        g.FillRectangle(&progTrackBrush, cardRect.X, cardRect.GetBottom() - progHeight, cardRect.Width, progHeight);

        if (frac > 0.0) {
            Gdiplus::SolidBrush progFillBrush(Gdiplus::Color(toast.is_hovered ? 245 : 195,
                                                                accentColor.GetR(), accentColor.GetG(), accentColor.GetB()));
            g.FillRectangle(&progFillBrush, cardRect.X, cardRect.GetBottom() - progHeight,
                            (float)(cardRect.Width * frac), progHeight);
        }

        g.ResetClip();

        // card border
        Gdiplus::Color borderColor = toast.is_hovered
            ? Gdiplus::Color(200, accentColor.GetR(), accentColor.GetG(), accentColor.GetB())
            : (isDark ? Gdiplus::Color(255, 52, 61, 82) : Gdiplus::Color(255, 220, 226, 238));
        Gdiplus::Pen borderPen(borderColor, toast.is_hovered ? 1.4f : 1.0f);
        g.DrawPath(&borderPen, &cardPath);

        // avatar
        float avatarSize = 46.0f;
        float avatarX = cardRect.X + 22.0f;
        float avatarY = cardRect.Y + (cardRect.Height - avatarSize) / 2.0f - 1.0f;
        Utils::DrawAvatarCircle(g, toast.avatar_png, toast.username, toast.status,
                                avatarX, avatarY, avatarSize, isDark, toast.is_summary || isSystemToast);

        // badges
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font badgeFont(&fontFamily, 7.8f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::Font titleFont(&fontFamily, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::Font subFont(&fontFamily, 8.8f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

        float textX = avatarX + avatarSize + 13.0f;
        float maxTextRight = cardRect.GetRight() - 34.0f;

        std::wstring statusLabel;
        if (toast.is_summary) statusLabel = L"SESSION SCAN";
        else if (isSystemToast) statusLabel = L"CLIPBOARD";
        else if (isEnemy) statusLabel = L"\u25CF ENEMY";
        else statusLabel = L"\u25CF ALLY";

        Gdiplus::RectF measuredBadge;
        g.MeasureString(statusLabel.c_str(), -1, &badgeFont, Gdiplus::PointF(0, 0), &measuredBadge);
        float pillW = measuredBadge.Width + 12.0f;
        float pillH = 17.0f;
        float pillY = cardRect.Y + 11.0f;

        Gdiplus::RectF statusPillRect(textX, pillY, pillW, pillH);
        Gdiplus::GraphicsPath statusPillPath;
        Utils::AddRoundedRect(statusPillPath, statusPillRect, 6.0f);

        Gdiplus::Color pillBg(isDark ? 45 : 28, accentColor.GetR(), accentColor.GetG(), accentColor.GetB());
        Gdiplus::Color pillFg = isEnemy
            ? (isDark ? Gdiplus::Color(255, 255, 120, 120) : Gdiplus::Color(255, 200, 38, 38))
            : (isSystemToast
                ? (isDark ? Gdiplus::Color(255, 140, 170, 255) : Gdiplus::Color(255, 65, 98, 225))
                : (isDark ? Gdiplus::Color(255, 94, 234, 174) : Gdiplus::Color(255, 16, 135, 92)));

        Gdiplus::SolidBrush pillBgBrush(pillBg);
        g.FillPath(&pillBgBrush, &statusPillPath);

        Gdiplus::StringFormat sfCenter;
        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush pillFgBrush(pillFg);
        g.DrawString(statusLabel.c_str(), -1, &badgeFont, statusPillRect, &sfCenter, &pillFgBrush);

        if (!toast.is_summary && !isSystemToast && config.notification.show_clan && !toast.clan.empty()) {
            float clanX = statusPillRect.GetRight() + 6.0f;
            Gdiplus::RectF measuredClan;
            g.MeasureString(toast.clan.c_str(), -1, &badgeFont, Gdiplus::PointF(0, 0), &measuredClan);
            float clanW = std::min(measuredClan.Width + 12.0f, std::max(40.0f, maxTextRight - clanX));
            if (clanX + 35.0f < maxTextRight) {
                Gdiplus::RectF clanPillRect(clanX, pillY, clanW, pillH);
                Gdiplus::GraphicsPath clanPillPath;
                Utils::AddRoundedRect(clanPillPath, clanPillRect, 6.0f);

                Gdiplus::Color clanBg = isDark ? Gdiplus::Color(255, 45, 53, 70) : Gdiplus::Color(255, 234, 238, 247);
                Gdiplus::Color clanFg = isDark ? Gdiplus::Color(255, 190, 200, 220) : Gdiplus::Color(255, 85, 96, 120);
                Gdiplus::SolidBrush clanBgBrush(clanBg);
                Gdiplus::SolidBrush clanFgBrush(clanFg);
                g.FillPath(&clanBgBrush, &clanPillPath);

                Gdiplus::StringFormat sfClan;
                sfClan.SetAlignment(Gdiplus::StringAlignmentCenter);
                sfClan.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                sfClan.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                sfClan.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                g.DrawString(toast.clan.c_str(), -1, &badgeFont, clanPillRect, &sfClan, &clanFgBrush);
            }
        }

        Gdiplus::SolidBrush titleBrush(textColor);
        Gdiplus::RectF titleRect(textX, cardRect.Y + 30.0f, std::max(60.0f, maxTextRight - textX + 14.0f), 22.0f);
        Gdiplus::StringFormat sfLeft;
        sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
        sfLeft.SetLineAlignment(Gdiplus::StringAlignmentNear);
        sfLeft.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        sfLeft.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        g.DrawString(toast.username.c_str(), -1, &titleFont, titleRect, &sfLeft, &titleBrush);

        std::wstring subText;
        if (toast.is_summary || isSystemToast) {
            subText = toast.clan;
            if (toast.is_hovered && !isSystemToast) {
                subText += L"  \u00B7  Click to open";
            }
        } else {
            if (toast.is_hovered) {
                subText = L"Click to open BorderWatch  \u00B7  \u00D7 to dismiss";
            } else {
                std::wstring actDesc = isEnemy ? L"Enemy spotted in server" : L"Ally spotted in server";
                subText = actDesc + std::wstring(L"  \u00B7  Click to open");
            }
        }

        Gdiplus::Color subColor = toast.is_hovered
            ? (isDark ? Gdiplus::Color(255, 200, 212, 235) : Gdiplus::Color(255, 70, 82, 110))
            : (isDark ? Gdiplus::Color(255, 148, 160, 184) : Gdiplus::Color(255, 120, 130, 152));
        Gdiplus::SolidBrush subBrush(subColor);
        Gdiplus::RectF subRect(textX, cardRect.Y + 51.0f, std::max(60.0f, cardRect.GetRight() - textX - 10.0f), 18.0f);
        g.DrawString(subText.c_str(), -1, &subFont, subRect, &sfLeft, &subBrush);

        float closeX = cardRect.GetRight() - 28.0f;
        float closeY = cardRect.Y + 8.0f;
        Gdiplus::RectF closeRect(closeX, closeY, 20.0f, 20.0f);

        if (toast.close_hovered || toast.is_hovered) {
            Gdiplus::GraphicsPath closePath;
            Utils::AddRoundedRect(closePath, closeRect, 6.0f);
            Gdiplus::Color closeBg = toast.close_hovered
                ? (isDark ? Gdiplus::Color(230, 225, 65, 75) : Gdiplus::Color(230, 235, 72, 77))
                : (isDark ? Gdiplus::Color(120, 48, 56, 74) : Gdiplus::Color(140, 232, 236, 245));
            Gdiplus::SolidBrush closeBgBrush(closeBg);
            g.FillPath(&closeBgBrush, &closePath);
        }

        Gdiplus::Color xColor = toast.close_hovered
            ? Gdiplus::Color(255, 255, 255, 255)
            : (isDark ? Gdiplus::Color(toast.is_hovered ? 230 : 140, 165, 176, 198)
                        : Gdiplus::Color(toast.is_hovered ? 230 : 140, 115, 125, 146));
        Gdiplus::Pen xPen(xColor, 1.6f);
        xPen.SetStartCap(Gdiplus::LineCapRound);
        xPen.SetEndCap(Gdiplus::LineCapRound);
        float cx = closeRect.X + closeRect.Width * 0.5f;
        float cy = closeRect.Y + closeRect.Height * 0.5f;
        float r = 4.0f;
        g.DrawLine(&xPen, cx - r, cy - r, cx + r, cy + r);
        g.DrawLine(&xPen, cx + r, cy - r, cx - r, cy + r);
    }

    // alpha channel premultiplication for UpdateLayeredWindow
    BYTE* pixels = static_cast<BYTE*>(pBits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            BYTE* px = pixels + (y * width + x) * 4;
            BYTE a = px[3];
            if (a > 0) {
                px[0] = (BYTE)((px[0] * a) / 255);
                px[1] = (BYTE)((px[1] * a) / 255);
                px[2] = (BYTE)((px[2] * a) / 255);
            }
        }
    }

    // update layered window
    POINT ptSrc = { 0, 0 };
    SIZE szWindow = { width, height };
    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = (BYTE)std::clamp(toast.current_opacity * 255.0, 0.0, 255.0);
    bf.AlphaFormat = AC_SRC_ALPHA;

    RECT rcWnd;
    GetWindowRect(toast.hWnd, &rcWnd);
    POINT ptDst = { rcWnd.left, rcWnd.top };

    UpdateLayeredWindow(toast.hWnd, hdcScreen, &ptDst, &szWindow, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void ToastManager::RecalculatePositions(const Config& config) {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    int cardWidth = config.notification.width + (int)(kToastMargin * 2.0f);
    int cardHeight = kToastWindowHeight;
    int margin = config.notification.margin;
    int stepY = cardHeight - 10;
    int stepX = cardWidth + 8;

    int availHeight = (wa.bottom - wa.top);
    int rows = std::max(1, (availHeight - 2 * margin) / stepY);

    for (size_t i = 0; i < m_toasts.size(); ++i) {
        auto& t = m_toasts[i];
        int slot = (int)i;
        int row = slot % rows;
        int col = slot / rows;

        int x = 0, y = 0;
        if (config.notification.corner == L"top-left") {
            x = wa.left + margin + col * stepX;
            y = wa.top + margin + row * stepY;
        } else if (config.notification.corner == L"top-right") {
            x = wa.right - margin - cardWidth - col * stepX;
            y = wa.top + margin + row * stepY;
        } else if (config.notification.corner == L"bottom-left") {
            x = wa.left + margin + col * stepX;
            y = wa.bottom - margin - cardHeight - row * stepY;
        } else { // "bottom-right" default
            x = wa.right - margin - cardWidth - col * stepX;
            y = wa.bottom - margin - cardHeight - row * stepY;
        }

        y += t->slide_offset_y;
        SetWindowPos(t->hWnd, HWND_TOPMOST, x, y, cardWidth, cardHeight,
                        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE);
    }
}

void ToastManager::ShowToast(const std::wstring& username, const std::wstring& status,
                            const std::wstring& clan, bool isSummary, const Config& config,
                            const std::vector<uint8_t>& avatarPng) {
    m_lastConfig = config;

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int cardWidth = config.notification.width + (int)(kToastMargin * 2.0f);
    int cardHeight = kToastWindowHeight;
    int margin = config.notification.margin;
    int stepY = std::max(1, cardHeight - 10);
    int stepX = cardWidth + 8;
    int availW = std::max(1, (int)(wa.right - wa.left));
    int availH = std::max(1, (int)(wa.bottom - wa.top));
    int rows = 1 + std::max(0, (availH - 2 * margin - cardHeight) / stepY);
    int cols = 1 + std::max(0, (availW - 2 * margin - cardWidth) / stepX);
    int effectiveMax = std::clamp(std::min(config.notification.max_visible, rows * cols), 1, 20);

    while ((int)m_toasts.size() >= effectiveMax) {
        if (!m_toasts.empty()) {
            DestroyWindow(m_toasts.front()->hWnd);
            m_toasts.erase(m_toasts.begin());
        }
    }

    auto toast = std::make_unique<ToastCard>();
    toast->username = username;
    toast->status = status;
    toast->clan = clan;
    toast->is_summary = isSummary;
    toast->avatar_png = avatarPng;
    toast->created_time = std::chrono::steady_clock::now();
    toast->last_tick_time = toast->created_time;
    toast->total_duration_ms = std::max(1000.0, config.notification.duration_seconds * 1000.0);
    toast->remaining_ms = toast->total_duration_ms;
    toast->expires_time = toast->created_time + std::chrono::milliseconds((int)toast->total_duration_ms);
    toast->current_opacity = 0.0;
    toast->target_opacity = config.notification.opacity;
    toast->slide_offset_y = 14;
    toast->closing = false;

    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TOAST_CLASS_NAME,
        L"BorderWatch Toast",
        WS_POPUP,
        0, 0, cardWidth, cardHeight,
        nullptr, nullptr, m_hInstance, this
    );

    toast->hWnd = hWnd;
    ShowWindow(hWnd, SW_SHOWNOACTIVATE);

    m_toasts.push_back(std::move(toast));
    RecalculatePositions(config);
    RenderToast(*m_toasts.back(), config);
}

void ToastManager::UpdateAnimations(const Config& config, const LogScanner* scanner) {
    m_lastConfig = config;
    auto now = std::chrono::steady_clock::now();
    bool needReposition = false;

    for (auto it = m_toasts.begin(); it != m_toasts.end();) {
        auto& t = *it;
        double dtMs = std::chrono::duration<double, std::milli>(now - t->last_tick_time).count();
        t->last_tick_time = now;

        bool dirty = false;
        if (!t->closing) {
            if (!t->is_hovered) {
                t->remaining_ms = std::max(0.0, t->remaining_ms - dtMs);
                dirty = true; // animate progress bar
                if (t->remaining_ms <= 0.0) {
                    t->closing = true;
                }
            }
        }

        if (!t->is_summary && t->avatar_png.empty() && scanner) {
            auto av = scanner->GetAvatarForUser(t->username);
            if (!av.empty()) {
                t->avatar_png = std::move(av);
                dirty = true;
            }
        }

        if (t->closing) {
            t->current_opacity -= 0.08;
            dirty = true;
            if (t->current_opacity <= 0.0) {
                DestroyWindow(t->hWnd);
                it = m_toasts.erase(it);
                needReposition = true;
                continue;
            }
        } else {
            if (t->current_opacity < t->target_opacity) {
                t->current_opacity = std::min(t->target_opacity, t->current_opacity + 0.09);
                dirty = true;
            }
            if (t->slide_offset_y > 0) {
                t->slide_offset_y = std::max(0, t->slide_offset_y - 3);
                needReposition = true;
                dirty = true;
            }
        }

        if (dirty) {
            RenderToast(*t, config);
        }
        ++it;
    }

    if (needReposition) {
        RecalculatePositions(config);
    }
}
