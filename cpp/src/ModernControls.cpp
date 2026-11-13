#include "../include/ModernControls.h"
#include "../include/Utils.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mmsystem.h>

static bool s_isDark = false;
static float s_darkProgress = 0.0f;

void ModernControls_SetDarkMode(bool isDark) {
    s_isDark = isDark;
    s_darkProgress = isDark ? 1.0f : 0.0f;
}

void ModernControls_SetDarkProgress(float progress) {
    s_darkProgress = std::clamp(progress, 0.0f, 1.0f);
    s_isDark = (s_darkProgress >= 0.5f);
}


// toggle switch

struct ToggleData {
    bool checked = false;
    std::wstring label;
    bool isHovered = false;
    float currentPos = 0.0f; // 0.0 (off) / 1.0 (on)
    float targetPos = 0.0f;
    float animTime = 0.0f;
    bool animating = false;
    std::chrono::steady_clock::time_point lastTick;
};

static LRESULT CALLBACK ToggleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ToggleData* data = reinterpret_cast<ToggleData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        data = new ToggleData();
        data->label = cs->lpszName ? cs->lpszName : L"";
        data->currentPos = 0.0f;
        data->targetPos = 0.0f;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        return TRUE;
    }
    case WM_NCDESTROY: {
        delete data;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    case WM_SETTEXT: {
        if (data) {
            data->label = lParam ? reinterpret_cast<LPCWSTR>(lParam) : L"";
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return TRUE;
    }
    case WM_SETCURSOR: {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        if (data && !data->isHovered) {
            data->isHovered = true;
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (data && data->isHovered) {
            data->isHovered = false;
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (data) {
            data->checked = !data->checked;
            data->targetPos = data->checked ? 1.0f : 0.0f;
            data->animating = true;
            data->animTime = 0.0f;
            data->lastTick = std::chrono::steady_clock::now();
            SetTimer(hWnd, 1, 8, nullptr);
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
            HWND hParent = GetParent(hWnd);
            if (hParent) {
                int id = (int)GetWindowLongPtrW(hWnd, GWLP_ID);
                PostMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)hWnd);
            }
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 1 && data && data->animating) {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - data->lastTick).count();
            data->lastTick = now;
            data->animTime += dt;

            const float duration = 0.13f;
            float progress = std::clamp(data->animTime / duration, 0.0f, 1.0f);
            float eased = Utils::EaseOutBack(progress);

            float start = data->checked ? 0.0f : 1.0f;
            float end = data->checked ? 1.0f : 0.0f;
            data->currentPos = start + (end - start) * eased;

            if (progress >= 1.0f) {
                data->currentPos = data->targetPos;
                data->animating = false;
                KillTimer(hWnd, 1);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
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

            Gdiplus::Color bgLight(255, 255, 255, 255);
            Gdiplus::Color bgDark(255, 30, 35, 48);
            Gdiplus::Color bg = Utils::LerpColor(bgLight, bgDark, s_darkProgress);
            g.Clear(bg);

            float trackW = 38.0f;
            float trackH = 20.0f;
            float trackX = 0.0f;
            float trackY = (h - trackH) / 2.0f;

            bool hovered = (data && data->isHovered);
            Gdiplus::Color offLight = hovered ? Gdiplus::Color(255, 206, 214, 228) : Gdiplus::Color(255, 220, 225, 236);
            Gdiplus::Color offDark  = hovered ? Gdiplus::Color(255, 82, 92, 116)   : Gdiplus::Color(255, 64, 73, 94);
            Gdiplus::Color offTrack = Utils::LerpColor(offLight, offDark, s_darkProgress);
            Gdiplus::Color onTrack  = hovered ? Gdiplus::Color(255, 102, 132, 248) : Gdiplus::Color(255, 85, 117, 239);

            float blend = std::clamp(data ? data->currentPos : 0.0f, 0.0f, 1.0f);
            Gdiplus::Color trackColor = Utils::LerpColor(offTrack, onTrack, blend);

            Gdiplus::GraphicsPath trackPath;
            Utils::AddRoundedRect(trackPath, Gdiplus::RectF(trackX, trackY, trackW, trackH), trackH / 2.0f);
            Gdiplus::SolidBrush trackBrush(trackColor);
            g.FillPath(&trackBrush, &trackPath);

            float thumbD = 16.0f;
            float travel = trackW - thumbD - 4.0f;
            float pos = data ? data->currentPos : (data && data->checked ? 1.0f : 0.0f);
            float thumbX = trackX + 2.0f + pos * travel;
            float thumbY = trackY + 2.0f;

            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(40, 0, 0, 0));
            g.FillEllipse(&shadowBrush, thumbX, thumbY + 1.0f, thumbD, thumbD);

            Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&thumbBrush, thumbX, thumbY, thumbD, thumbD);

            if (data && !data->label.empty()) {
                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font font(&ff, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::Color textLight(255, 36, 41, 56);
                Gdiplus::Color textDark(255, 241, 243, 250);
                Gdiplus::Color textColor = Utils::LerpColor(textLight, textDark, s_darkProgress);
                Gdiplus::SolidBrush textBrush(textColor);

                Gdiplus::StringFormat sf;
                sf.SetAlignment(Gdiplus::StringAlignmentNear);
                sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

                Gdiplus::RectF textRect(trackW + 10.0f, 0.0f, (float)(w - trackW - 10.0f), (float)h);
                g.DrawString(data->label.c_str(), -1, &font, textRect, &sf, &textBrush);
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

void Toggle_SetChecked(HWND hWnd, bool checked) {
    ToggleData* data = reinterpret_cast<ToggleData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        if (data->checked != checked) {
            data->checked = checked;
            data->targetPos = checked ? 1.0f : 0.0f;
            data->animating = true;
            data->animTime = 0.0f;
            data->lastTick = std::chrono::steady_clock::now();
            SetTimer(hWnd, 1, 8, nullptr);
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
        } else {
            data->currentPos = checked ? 1.0f : 0.0f;
            data->targetPos = data->currentPos;
            data->animating = false;
            KillTimer(hWnd, 1);
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
        }
    }
}

bool Toggle_GetChecked(HWND hWnd) {
    ToggleData* data = reinterpret_cast<ToggleData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    return data ? data->checked : false;
}


// cool slider idk

struct SliderData {
    double value = 0.5; // 0.0 to 1.0
    bool dragging = false;
};

static LRESULT CALLBACK SliderWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SliderData* data = reinterpret_cast<SliderData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        data = new SliderData();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        return TRUE;
    }
    case WM_NCDESTROY: {
        delete data;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    case WM_SETCURSOR: {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        if (data) {
            data->dragging = true;
            SetCapture(hWnd);

            RECT rc;
            GetClientRect(hWnd, &rc);
            int trackW = (rc.right - rc.left) - 45;
            int x = LOWORD(lParam) - 7;
            data->value = std::clamp((double)x / (double)(trackW - 14), 0.0, 1.0);
            InvalidateRect(hWnd, nullptr, FALSE);

            HWND hParent = GetParent(hWnd);
            if (hParent) {
                int id = (int)GetWindowLongPtrW(hWnd, GWLP_ID);
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, 0), (LPARAM)hWnd);
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (data && data->dragging) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int trackW = (rc.right - rc.left) - 45;
            short x = (short)LOWORD(lParam) - 7;
            data->value = std::clamp((double)x / (double)(trackW - 14), 0.0, 1.0);
            InvalidateRect(hWnd, nullptr, FALSE);

            HWND hParent = GetParent(hWnd);
            if (hParent) {
                int id = (int)GetWindowLongPtrW(hWnd, GWLP_ID);
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, 0), (LPARAM)hWnd);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (data && data->dragging) {
            data->dragging = false;
            ReleaseCapture();
            HWND hParent = GetParent(hWnd);
            if (hParent) {
                int id = (int)GetWindowLongPtrW(hWnd, GWLP_ID);
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, 1), (LPARAM)hWnd);
            }
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
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

            Gdiplus::Color bg = s_isDark ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 255, 255, 255);
            g.Clear(bg);

            float textW = 46.0f;
            float trackW = (float)w - textW - 4.0f;
            float trackH = 6.0f;
            float trackX = 6.0f;
            float trackY = (h - trackH) / 2.0f;
            float innerW = trackW - 12.0f;

            // inactive track
            Gdiplus::GraphicsPath trackPath;
            Utils::AddRoundedRect(trackPath, Gdiplus::RectF(trackX, trackY, innerW, trackH), 3.0f);
            Gdiplus::Color inactCol = s_isDark ? Gdiplus::Color(255, 52, 60, 80) : Gdiplus::Color(255, 220, 225, 236);
            Gdiplus::SolidBrush inactBrush(inactCol);
            g.FillPath(&inactBrush, &trackPath);

            // active track
            double val = data ? data->value : 0.5;
            float fillW = (float)(val * innerW);
            if (fillW > 0) {
                Gdiplus::GraphicsPath fillPath;
                Utils::AddRoundedRect(fillPath, Gdiplus::RectF(trackX, trackY, fillW, trackH), 3.0f);
                Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 85, 117, 239));
                g.FillPath(&fillBrush, &fillPath);
            }

            // circular knob
            float thumbR = 7.0f;
            float thumbX = trackX + fillW - thumbR;
            float thumbY = (h / 2.0f) - thumbR;

            Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&knobBrush, thumbX, thumbY, thumbR * 2.0f, thumbR * 2.0f);

            Gdiplus::Pen knobBorder(Gdiplus::Color(255, 85, 117, 239), 1.5f);
            g.DrawEllipse(&knobBorder, thumbX, thumbY, thumbR * 2.0f, thumbR * 2.0f);

            // percentage capsule badge
            int pct = (int)(val * 100.0 + 0.5);
            std::wstring pctStr = std::to_wstring(pct) + L"%";

            Gdiplus::RectF pillRect(trackW + 4.0f, 2.0f, textW - 2.0f, (float)(h - 4));
            Gdiplus::GraphicsPath pillPath;
            Utils::AddRoundedRect(pillPath, pillRect, 6.0f);
            Gdiplus::SolidBrush pillBg(s_isDark ? Gdiplus::Color(255, 42, 49, 66) : Gdiplus::Color(255, 237, 240, 248));
            g.FillPath(&pillBg, &pillPath);

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font font(&ff, 8.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::Color txtCol = s_isDark ? Gdiplus::Color(255, 195, 206, 228) : Gdiplus::Color(255, 72, 82, 106);
            Gdiplus::SolidBrush txtBrush(txtCol);

            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(pctStr.c_str(), -1, &font, pillRect, &sf, &txtBrush);
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

void Slider_SetValue(HWND hWnd, double val) {
    SliderData* data = reinterpret_cast<SliderData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->value = std::clamp(val, 0.0, 1.0);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

double Slider_GetValue(HWND hWnd) {
    SliderData* data = reinterpret_cast<SliderData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    return data ? data->value : 0.5;
}


// [-] [+]

struct StepperData {
    int value = 5;
    int minVal = 1;
    int maxVal = 30;
    std::wstring suffix = L"s";
    int hoverBtn = 0; // 0 none, 1 minus, 2 plus
};

static LRESULT CALLBACK StepperWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StepperData* data = reinterpret_cast<StepperData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        data = new StepperData();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        return TRUE;
    }
    case WM_NCDESTROY: {
        delete data;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    case WM_SETCURSOR: {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        if (data) {
            int x = LOWORD(lParam);
            int btnW = 24;
            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right - rc.left;

            if (x <= btnW) {
                // minus
                if (data->value > data->minVal) {
                    data->value--;
                    InvalidateRect(hWnd, nullptr, FALSE);
                    HWND hParent = GetParent(hWnd);
                    if (hParent) {
                        int id = (int)GetWindowLongPtrW(hWnd, GWLP_ID);
                        SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, 0), (LPARAM)hWnd);
                    }
                }
            } else if (x >= w - btnW) {
                // plus
                if (data->value < data->maxVal) {
                    data->value++;
                    InvalidateRect(hWnd, nullptr, FALSE);
                    HWND hParent = GetParent(hWnd);
                    if (hParent) {
                        int id = (int)GetWindowLongPtrW(hWnd, GWLP_ID);
                        SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, 0), (LPARAM)hWnd);
                    }
                }
            }
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
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

            Gdiplus::Color bg = s_isDark ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 255, 255, 255);
            g.Clear(bg);

            float btnW = 24.0f;
            float btnH = (float)h;

            Gdiplus::Color btnBg = s_isDark ? Gdiplus::Color(255, 48, 55, 72) : Gdiplus::Color(255, 237, 240, 246);
            Gdiplus::Color btnTxt = s_isDark ? Gdiplus::Color(255, 223, 229, 242) : Gdiplus::Color(255, 55, 64, 86);

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font btnFont(&ff, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush btnTxtBrush(btnTxt);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            // minus button
            Gdiplus::GraphicsPath minusPath;
            Utils::AddRoundedRect(minusPath, Gdiplus::RectF(0, 0, btnW, btnH), 6.0f);
            Gdiplus::SolidBrush btnBrush(btnBg);
            g.FillPath(&btnBrush, &minusPath);
            g.DrawString(L"\u2212", -1, &btnFont, Gdiplus::RectF(0, 0, btnW, btnH), &sf, &btnTxtBrush);

            // plus button
            Gdiplus::GraphicsPath plusPath;
            Utils::AddRoundedRect(plusPath, Gdiplus::RectF((float)(w - btnW), 0, btnW, btnH), 6.0f);
            g.FillPath(&btnBrush, &plusPath);
            g.DrawString(L"+", -1, &btnFont, Gdiplus::RectF((float)(w - btnW), 0, btnW, btnH), &sf, &btnTxtBrush);

            // center value
            std::wstring valStr = std::to_wstring(data ? data->value : 5) + (data ? data->suffix : L"");
            Gdiplus::Font valFont(&ff, 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::Color valCol = s_isDark ? Gdiplus::Color(255, 241, 243, 250) : Gdiplus::Color(255, 36, 41, 56);
            Gdiplus::SolidBrush valTxtBrush(valCol);
            g.DrawString(valStr.c_str(), -1, &valFont, Gdiplus::RectF(btnW, 0, (float)(w - 2 * btnW), (float)h), &sf, &valTxtBrush);
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

void Stepper_SetRange(HWND hWnd, int minVal, int maxVal, const std::wstring& suffix) {
    StepperData* data = reinterpret_cast<StepperData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->minVal = minVal;
        data->maxVal = maxVal;
        data->suffix = suffix;
        data->value = std::clamp(data->value, minVal, maxVal);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void Stepper_SetValue(HWND hWnd, int val) {
    StepperData* data = reinterpret_cast<StepperData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->value = std::clamp(val, data->minVal, data->maxVal);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

int Stepper_GetValue(HWND hWnd) {
    StepperData* data = reinterpret_cast<StepperData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    return data ? data->value : 0;
}


// flat dropdown selector

struct DropdownData {
    std::vector<std::wstring> items;
    int selectedIndex = 0;
    bool isHovered = false;
};

static LRESULT CALLBACK DropdownWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DropdownData* data = reinterpret_cast<DropdownData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        data = new DropdownData();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        return TRUE;
    }
    case WM_NCDESTROY: {
        delete data;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    case WM_SETCURSOR: {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        if (data && !data->isHovered) {
            data->isHovered = true;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (data && data->isHovered) {
            data->isHovered = false;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (data && !data->items.empty()) {
            HMENU hMenu = CreatePopupMenu();
            for (int i = 0; i < (int)data->items.size(); ++i) {
                UINT flags = MF_STRING;
                if (i == data->selectedIndex) flags |= MF_CHECKED;
                AppendMenuW(hMenu, flags, i + 1, data->items[i].c_str());
            }

            RECT rc;
            GetWindowRect(hWnd, &rc);
            int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                     rc.left, rc.bottom + 2, 0, hWnd, nullptr);
            DestroyMenu(hMenu);

            if (cmd > 0 && cmd <= (int)data->items.size()) {
                data->selectedIndex = cmd - 1;
                InvalidateRect(hWnd, nullptr, FALSE);

                HWND hParent = GetParent(hWnd);
                if (hParent) {
                    int id = (int)GetWindowLongPtrW(hWnd, GWLP_ID);
                    SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), (LPARAM)hWnd);
                }
            }
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
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

            // bg
            Gdiplus::Color bg = s_isDark ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 255, 255, 255);
            g.Clear(bg);

            Gdiplus::GraphicsPath path;
            Utils::AddRoundedRect(path, Gdiplus::RectF(0, 0, (float)w, (float)h), 8.0f);

            bool hovered = data && data->isHovered;
            Gdiplus::Color pillBg;
            if (s_isDark) {
                pillBg = hovered ? Gdiplus::Color(255, 57, 66, 85) : Gdiplus::Color(255, 48, 55, 72);
            } else {
                pillBg = hovered ? Gdiplus::Color(255, 242, 245, 250) : Gdiplus::Color(255, 237, 240, 246);
            }
            Gdiplus::SolidBrush pillBrush(pillBg);
            g.FillPath(&pillBrush, &path);

            Gdiplus::Color borderCol = s_isDark ? Gdiplus::Color(255, 68, 77, 98) : Gdiplus::Color(255, 220, 225, 236);
            Gdiplus::Pen borderPen(borderCol, 1.0f);
            g.DrawPath(&borderPen, &path);

            // display selected item text
            std::wstring curText;
            if (data && data->selectedIndex >= 0 && data->selectedIndex < (int)data->items.size()) {
                curText = data->items[data->selectedIndex];
            }

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font font(&ff, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::Color txtCol = s_isDark ? Gdiplus::Color(255, 223, 229, 242) : Gdiplus::Color(255, 55, 64, 86);
            Gdiplus::SolidBrush txtBrush(txtCol);

            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentNear);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

            g.DrawString(curText.c_str(), -1, &font, Gdiplus::RectF(10.0f, 0.0f, (float)(w - 28), (float)h), &sf, &txtBrush);

            // down arrow symbol
            Gdiplus::Font arrowFont(&ff, 7.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::Color arrowCol = s_isDark ? Gdiplus::Color(255, 161, 172, 194) : Gdiplus::Color(255, 135, 144, 165);
            Gdiplus::SolidBrush arrowBrush(arrowCol);
            Gdiplus::StringFormat sfRight;
            sfRight.SetAlignment(Gdiplus::StringAlignmentFar);
            sfRight.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            g.DrawString(L"\u25BE", -1, &arrowFont, Gdiplus::RectF(0.0f, 0.0f, (float)(w - 10), (float)h), &sfRight, &arrowBrush);
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

void Dropdown_SetItems(HWND hWnd, const std::vector<std::wstring>& items) {
    DropdownData* data = reinterpret_cast<DropdownData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->items = items;
        if (data->selectedIndex >= (int)items.size()) data->selectedIndex = 0;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void Dropdown_SetCurSel(HWND hWnd, int idx) {
    DropdownData* data = reinterpret_cast<DropdownData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->selectedIndex = idx;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

int Dropdown_GetCurSel(HWND hWnd) {
    DropdownData* data = reinterpret_cast<DropdownData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    return data ? data->selectedIndex : 0;
}

std::wstring Dropdown_GetSelectedText(HWND hWnd) {
    DropdownData* data = reinterpret_cast<DropdownData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data && data->selectedIndex >= 0 && data->selectedIndex < (int)data->items.size()) {
        return data->items[data->selectedIndex];
    }
    return L"";
}


// history component

struct HistoryData {
    std::vector<std::wstring> lines;
    float currentScroll = 0.0f;
    float targetScroll = 0.0f;
    bool scrollAnimating = false;
    LONGLONG lastScrollQpc = 0;
    bool isDragging = false;
    int dragStartY = 0;
    float dragStartOffset = 0.0f;
};

static LRESULT CALLBACK HistoryWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HistoryData* data = reinterpret_cast<HistoryData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        data = new HistoryData();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        return TRUE;
    }
    case WM_NCDESTROY: {
        if (data && data->scrollAnimating) {
            timeEndPeriod(1);
        }
        delete data;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR: {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    }
    case WM_MOUSEWHEEL: {
        if (data && !data->lines.empty()) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int h = (int)(rc.bottom - rc.top);
            int visibleCount = std::max(1, (h - 14) / 18);
            int maxOffset = std::max(0, (int)data->lines.size() - visibleCount);

            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float linesToScroll = (float)delta / 120.0f * 2.5f;
            data->targetScroll = std::clamp(data->targetScroll - linesToScroll, 0.0f, (float)maxOffset);

            if (!data->scrollAnimating) {
                data->scrollAnimating = true;
                LARGE_INTEGER li;
                QueryPerformanceCounter(&li);
                data->lastScrollQpc = li.QuadPart;
                timeBeginPeriod(1);
                SetTimer(hWnd, 2, 8, nullptr);
            }
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 2 && data && data->scrollAnimating) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            float dt = (float)(now.QuadPart - data->lastScrollQpc) / (float)freq.QuadPart;
            data->lastScrollQpc = now.QuadPart;
            dt = std::clamp(dt, 0.001f, 0.05f);

            float diff = data->targetScroll - data->currentScroll;
            if (std::abs(diff) < 0.002f) {
                data->currentScroll = data->targetScroll;
                data->scrollAnimating = false;
                KillTimer(hWnd, 2);
                timeEndPeriod(1);
            } else {
                float factor = 1.0f - std::exp(-16.0f * dt);
                data->currentScroll += diff * factor;
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            UpdateWindow(hWnd);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (data && !data->lines.empty()) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right - rc.left;
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            if (x >= w - 16) {
                // scrollbar region click / drag
                data->isDragging = true;
                data->dragStartY = y;
                data->dragStartOffset = data->currentScroll;
                SetCapture(hWnd);
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (data && data->isDragging) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int h = rc.bottom - rc.top;
            int visibleCount = std::max(1, (h - 14) / 18);
            int maxOffset = std::max(0, (int)data->lines.size() - visibleCount);
            if (maxOffset > 0) {
                int dy = (short)HIWORD(lParam) - data->dragStartY;
                float trackH = (float)(h - 16);
                float deltaOffset = (float)dy / trackH * (float)maxOffset;
                data->currentScroll = std::clamp(data->dragStartOffset + deltaOffset, 0.0f, (float)maxOffset);
                data->targetScroll = data->currentScroll;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (data && data->isDragging) {
            data->isDragging = false;
            ReleaseCapture();
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

            Gdiplus::Color bgLight(255, 255, 255, 255);
            Gdiplus::Color bgDark(255, 30, 35, 48);
            Gdiplus::Color bg = Utils::LerpColor(bgLight, bgDark, s_darkProgress);
            g.Clear(bg);

            Gdiplus::GraphicsPath boxPath;
            Utils::AddRoundedRect(boxPath, Gdiplus::RectF(0.5f, 0.5f, (float)(w - 1), (float)(h - 1)), 8.0f);
            Gdiplus::SolidBrush boxBrush(bg);
            g.FillPath(&boxBrush, &boxPath);

            Gdiplus::Color bLight(255, 227, 231, 240);
            Gdiplus::Color bDark(255, 52, 60, 78);
            Gdiplus::Color borderCol = Utils::LerpColor(bLight, bDark, s_darkProgress);
            Gdiplus::Pen borderPen(borderCol, 1.0f);
            g.DrawPath(&borderPen, &boxPath);

            Gdiplus::FontFamily ff(L"Segoe UI");

            if (!data || data->lines.empty()) {
                Gdiplus::Font emptyFont(&ff, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::Color eLight(255, 150, 158, 175);
                Gdiplus::Color eDark(255, 135, 144, 165);
                Gdiplus::Color emptyCol = Utils::LerpColor(eLight, eDark, s_darkProgress);
                Gdiplus::SolidBrush emptyBrush(emptyCol);

                Gdiplus::StringFormat sf;
                sf.SetAlignment(Gdiplus::StringAlignmentNear);
                sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(L"No players detected yet in this session.", -1, &emptyFont,
                                Gdiplus::RectF(14.0f, 0.0f, (float)(w - 28), (float)h), &sf, &emptyBrush);
            } else {
                int lineH = 18;
                float startY = 8.0f;
                int visibleCount = std::max(1, (h - 14) / lineH);
                int maxOffset = std::max(0, (int)data->lines.size() - visibleCount);
                data->currentScroll = std::clamp(data->currentScroll, 0.0f, (float)maxOffset);

                g.SetClip(Gdiplus::RectF(2.0f, 4.0f, (float)(w - 4), (float)(h - 8)));

                Gdiplus::Font fontTime(&ff, 8.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::Font fontUser(&ff, 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fontBadge(&ff, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fontClan(&ff, 8.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

                Gdiplus::Color timeLight(255, 150, 158, 175);
                Gdiplus::Color timeDark(255, 135, 144, 165);
                Gdiplus::Color timeCol = Utils::LerpColor(timeLight, timeDark, s_darkProgress);

                Gdiplus::Color userLight(255, 36, 41, 56);
                Gdiplus::Color userDark(255, 241, 243, 250);
                Gdiplus::Color userCol = Utils::LerpColor(userLight, userDark, s_darkProgress);

                Gdiplus::Color clanLight(255, 100, 110, 130);
                Gdiplus::Color clanDark(255, 161, 172, 194);
                Gdiplus::Color clanCol = Utils::LerpColor(clanLight, clanDark, s_darkProgress);

                Gdiplus::SolidBrush timeBrush(timeCol);
                Gdiplus::SolidBrush userBrush(userCol);
                Gdiplus::SolidBrush clanBrush(clanCol);

                Gdiplus::StringFormat sfNear;
                sfNear.SetAlignment(Gdiplus::StringAlignmentNear);
                sfNear.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                sfNear.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

                int startIdx = (int)std::floor(data->currentScroll);
                int endIdx = std::min((int)data->lines.size(), startIdx + visibleCount + 2);
                for (int i = startIdx; i < endIdx; ++i) {
                    float curY = startY + ((float)i - data->currentScroll) * (float)lineH;
                    const std::wstring& raw = data->lines[i];

                    size_t slash1 = raw.find(L" / ");
                    if (slash1 != std::wstring::npos) {
                        size_t slash2 = raw.find(L" / ", slash1 + 3);
                        std::wstring partTimeUser = raw.substr(0, slash1);
                        std::wstring partStatus = (slash2 != std::wstring::npos) ? raw.substr(slash1 + 3, slash2 - (slash1 + 3)) : raw.substr(slash1 + 3);
                        std::wstring partClan = (slash2 != std::wstring::npos) ? raw.substr(slash2 + 3) : L"";

                        std::wstring pTime, pUser;
                        size_t sp = partTimeUser.find(L"  ");
                        if (sp != std::wstring::npos) {
                            pTime = partTimeUser.substr(0, sp);
                            pUser = partTimeUser.substr(sp + 2);
                        } else {
                            pUser = partTimeUser;
                        }

                        float drawX = 12.0f;
                        if (!pTime.empty()) {
                            g.DrawString(pTime.c_str(), -1, &fontTime, Gdiplus::PointF(drawX, curY), &timeBrush);
                            drawX += 62.0f;
                        }

                        g.DrawString(pUser.c_str(), -1, &fontUser, Gdiplus::PointF(drawX, curY), &userBrush);
                        Gdiplus::RectF userBounds;
                        g.MeasureString(pUser.c_str(), -1, &fontUser, Gdiplus::PointF(drawX, curY), &userBounds);
                        drawX += userBounds.Width + 8.0f;

                        bool isEnemy = (partStatus == L"Enemy");
                        std::wstring pillText = isEnemy ? L"\u25CF ENEMY" : L"\u25CF ALLY";
                        Gdiplus::Color badgeBg = isEnemy ? (s_isDark ? Gdiplus::Color(255, 55, 26, 32) : Gdiplus::Color(255, 254, 240, 242))
                                                            : (s_isDark ? Gdiplus::Color(255, 25, 36, 30) : Gdiplus::Color(255, 243, 248, 245));
                        Gdiplus::Color badgeTxt = isEnemy
                            ? (s_isDark ? Gdiplus::Color(255, 255, 125, 130) : Gdiplus::Color(255, 195, 45, 55))
                            : (s_isDark ? Gdiplus::Color(255, 120, 200, 155) : Gdiplus::Color(255, 52, 126, 85));

                        Gdiplus::RectF badgeBounds;
                        g.MeasureString(pillText.c_str(), -1, &fontBadge, Gdiplus::PointF(0, 0), &badgeBounds);
                        float badgeW = badgeBounds.Width + 10.0f;
                        float badgeH = 15.0f;

                        Gdiplus::GraphicsPath bp;
                        Utils::AddRoundedRect(bp, Gdiplus::RectF(drawX, curY + 1.0f, badgeW, badgeH), 4.5f);
                        Gdiplus::SolidBrush bBrush(badgeBg);
                        g.FillPath(&bBrush, &bp);

                        Gdiplus::SolidBrush bTxtBrush(badgeTxt);
                        Gdiplus::StringFormat sfCenter;
                        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
                        sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                        g.DrawString(pillText.c_str(), -1, &fontBadge, Gdiplus::RectF(drawX, curY + 1.0f, badgeW, badgeH), &sfCenter, &bTxtBrush);
                        drawX += badgeW + 8.0f;

                        if (!partClan.empty()) {
                            g.DrawString((L"\u00B7  " + partClan).c_str(), -1, &fontClan, Gdiplus::PointF(drawX, curY), &clanBrush);
                        }
                    } else {

                        g.DrawString(raw.c_str(), -1, &fontTime, Gdiplus::RectF(12.0f, curY, (float)(w - 28), (float)lineH), &sfNear, &userBrush);
                    }
                }

                g.ResetClip();

                if ((int)data->lines.size() > visibleCount && maxOffset > 0) {
                    float trackX = (float)(w - 8);
                    float trackY = 8.0f;
                    float trackW = 4.0f;
                    float trackH = (float)(h - 16);

                    float thumbH = std::max(14.0f, ((float)visibleCount / (float)data->lines.size()) * trackH);
                    float thumbY = trackY + (data->currentScroll / (float)maxOffset) * (trackH - thumbH);

                    Gdiplus::GraphicsPath thumbPath;
                    Utils::AddRoundedRect(thumbPath, Gdiplus::RectF(trackX, thumbY, trackW, thumbH), 2.0f);

                    Gdiplus::Color thumbCol = s_isDark ? Gdiplus::Color(140, 100, 115, 145) : Gdiplus::Color(160, 180, 190, 205);
                    Gdiplus::SolidBrush thumbBrush(thumbCol);
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

void History_SetLines(HWND hWnd, const std::vector<std::wstring>& lines) {
    HistoryData* data = reinterpret_cast<HistoryData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->lines = lines;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void History_AddLine(HWND hWnd, const std::wstring& line) {
    HistoryData* data = reinterpret_cast<HistoryData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->lines.insert(data->lines.begin(), line);
        if (data->lines.size() > 50) data->lines.pop_back();
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void History_Clear(HWND hWnd) {
    HistoryData* data = reinterpret_cast<HistoryData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (data) {
        data->lines.clear();
        data->currentScroll = 0.0f;
        data->targetScroll = 0.0f;
        data->scrollAnimating = false;
        KillTimer(hWnd, 2);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}


// registration

void RegisterModernControls(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    wc.lpfnWndProc = ToggleWndProc;
    wc.lpszClassName = BW_TOGGLE_CLASS;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = SliderWndProc;
    wc.lpszClassName = BW_SLIDER_CLASS;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = StepperWndProc;
    wc.lpszClassName = BW_STEPPER_CLASS;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = DropdownWndProc;
    wc.lpszClassName = BW_DROPDOWN_CLASS;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = HistoryWndProc;
    wc.lpszClassName = BW_HISTORY_CLASS;
    RegisterClassExW(&wc);
}
