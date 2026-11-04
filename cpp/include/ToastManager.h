#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include "Config.h"

#define WM_BW_OPEN_FROM_TOAST (WM_USER + 105)

struct ToastCard {
    HWND hWnd = nullptr;
    int slot = 0;
    std::wstring username;
    std::wstring status;
    std::wstring clan;
    bool is_summary = false;
    std::vector<uint8_t> avatar_png;
    double current_opacity = 0.0;
    double target_opacity = 1.0;
    int slide_offset_y = 18;
    bool closing = false;
    bool is_hovered = false;
    bool close_hovered = false;
    double total_duration_ms = 5500.0;
    double remaining_ms = 5500.0;
    std::chrono::steady_clock::time_point last_tick_time;
    std::chrono::steady_clock::time_point created_time;
    std::chrono::steady_clock::time_point expires_time;
};

class LogScanner;

class ToastManager {
public:
    ToastManager(HINSTANCE hInstance);
    ~ToastManager();

    void Initialize();
    void SetParentWindow(HWND hParentWnd) { m_hParentWnd = hParentWnd; }
    void ShowToast(const std::wstring& username, const std::wstring& status,
                    const std::wstring& clan, bool isSummary, const Config& config,
                    const std::vector<uint8_t>& avatarPng = {});
    void UpdateAnimations(const Config& config, const LogScanner* scanner = nullptr);
    bool HasActiveToasts() const { return !m_toasts.empty(); }
    void CloseAll();

private:
    static LRESULT CALLBACK ToastWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleToastMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    ToastCard* FindToastByHwnd(HWND hWnd);
    void RenderToast(ToastCard& toast, const Config& config);
    void RecalculatePositions(const Config& config);

    HINSTANCE m_hInstance;
    HWND m_hParentWnd = nullptr;
    Config m_lastConfig = Config::GetDefault();
    std::vector<std::unique_ptr<ToastCard>> m_toasts;
    ULONG_PTR m_gdiplusToken = 0;
};
