#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include "Config.h"
#include "Watchlist.h"
#include "LogScanner.h"
#include "AudioPlayer.h"
#include "ToastManager.h"
#include "DetectedWindow.h"
#include "WatchlistWindow.h"
#include "TrayIcon.h"
#include "ModernControls.h"
#include "AntiAfk.h"

extern const wchar_t* MAIN_CLASS_NAME;
void AppReleaseMutex();

#define WM_BW_GITHUB_UPDATED      (WM_USER + 102)
#define WM_BW_APP_UPDATE_RESULT   (WM_USER + 106)
#define WM_BW_APP_UPDATE_PROGRESS (WM_USER + 107)

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance, const std::wstring& dataDir);
    ~MainWindow();

    bool Create();
    void Run(bool startHidden, bool preview = false);

    void ToggleVisibility();
    void ShowPanel();
    void HidePanel();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void RelayoutControls(int width, int height);
    void ApplyTheme();
    void SyncControlsFromConfig();
    void SaveConfigFromControls();
    void ProcessScannerEvents();
    void UpdateStatusDisplay();
    void ChooseCustomSound(bool isAlly = false);
    void UpdateSoundDropdownItems();
    void StartGitHubSync();
    void CheckForAppUpdates(bool manualTrigger = false);
    void PerformSelfUpdate();
    void EnsureAnimationTimer();
    void CopyServerDeepLink();
    void OpenWatchlistWindow();
    void SetSaveStatusText(const std::wstring& text, int autoClearMs = 2800);
    void DrawWindowBackground(HDC hdc, int width, int height);
    void DrawModernButton(LPDRAWITEMSTRUCT dis);

    HINSTANCE m_hInstance;
    std::wstring m_dataDir;
    HWND m_hWnd = nullptr;

    // components
    Config m_config;
    Watchlist m_watchlist;
    std::unique_ptr<LogScanner> m_scanner;
    std::unique_ptr<AudioPlayer> m_audio;
    std::unique_ptr<ToastManager> m_toasts;
    std::unique_ptr<DetectedWindow> m_detectedWnd;
    std::unique_ptr<WatchlistWindow> m_watchlistWnd;
    std::unique_ptr<TrayIcon> m_tray;
    std::unique_ptr<AntiAfk> m_antiAfk;

    std::thread m_githubThread;
    std::thread m_updateThread;
    std::mutex m_githubMutex;
    std::atomic<bool> m_githubSyncInFlight{ false };
    std::atomic<bool> m_updateInFlight{ false };
    bool m_updateAvailable = false;
    std::wstring m_latestVersion;
    std::wstring m_updateDownloadUrl;
    std::wstring m_updateStatusMsg;
    std::wstring m_githubStatusMsg = L"Auto-syncing GitHub watchlist...";
    std::wstring m_githubStatusErr;
    std::vector<std::wstring> m_historyLines;
    bool m_fastTimerActive = false;

    std::wstring m_lastStatusText = L"Getting ready...";
    std::wstring m_lastStatsText = L"Loading watchlist...";
    std::wstring m_lastCoverageText;
    std::wstring m_githubText = L"Auto-syncing GitHub watchlist...";
    std::wstring m_saveStatusText;

    // controls
    HWND m_hTooltip = nullptr;
    HWND m_hTopMinBtn = nullptr;
    HWND m_hTopCloseBtn = nullptr;
    HWND m_hAppUpdateBtn = nullptr;
    HWND m_hDeepLinkBtn = nullptr;
    HWND m_hDetectedBtn = nullptr;
    HWND m_hPauseBtn = nullptr;
    HWND m_hTestSoundBtn = nullptr;
    HWND m_hAutostartBtn = nullptr;
    HWND m_hQuitBtn = nullptr;

    HWND m_hEnemiesCheck = nullptr;
    HWND m_hAlliesCheck = nullptr;
    HWND m_hSoundCheck = nullptr;
    HWND m_hSoundCombo = nullptr;
    HWND m_hPreviewEnemyBtn = nullptr;
    HWND m_hChooseSoundBtn = nullptr;
    HWND m_hAllySoundCombo = nullptr;
    HWND m_hPreviewAllyBtn = nullptr;
    HWND m_hChooseAllySoundBtn = nullptr;
    HWND m_hVolumeSlider = nullptr;

    HWND m_hDarkModeCheck = nullptr;
    HWND m_hCornerCombo = nullptr;
    HWND m_hDurationEdit = nullptr;
    HWND m_hMaxVisibleEdit = nullptr;
    HWND m_hResetBtn = nullptr;
    HWND m_hHideTrayBtn = nullptr;
    HWND m_hHistoryBox = nullptr;

    // brushes
    HBRUSH m_hBgBrush = nullptr;
    HBRUSH m_hCardBrush = nullptr;
    HFONT m_hFontRegular = nullptr;
    HFONT m_hFontBold = nullptr;
    HFONT m_hFontTitle = nullptr;
    HFONT m_hFontHeader = nullptr;
};
