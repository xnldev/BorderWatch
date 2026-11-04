#include "../include/MainWindow.h"
#include "../include/Utils.h"
#include "../include/HotkeyManager.h"
#include "../include/StartupManager.h"
#include "../include/Resource.h"
#include "../third_party/yyjson.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <winhttp.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winhttp.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

const wchar_t* MAIN_CLASS_NAME = L"BorderWatchMainClass";

enum ControlId {
    IDC_BTN_TOP_MIN = 1000,
    IDC_BTN_TOP_CLOSE,
    IDC_BTN_DETECTED,
    IDC_BTN_PAUSE,
    IDC_BTN_TEST_SOUND,
    IDC_BTN_QUIT,
    IDC_CHK_ENEMIES,
    IDC_CHK_ALLIES,
    IDC_CHK_SOUND,
    IDC_CMB_SOUND,
    IDC_BTN_PREVIEW_ENEMY,
    IDC_BTN_CHOOSE_SOUND,
    IDC_CMB_ALLY_SOUND,
    IDC_BTN_PREVIEW_ALLY,
    IDC_BTN_CHOOSE_ALLY_SOUND,
    IDC_SLD_VOLUME,
    IDC_BTN_SAVE,
    IDC_CHK_DARK_MODE,
    IDC_CMB_CORNER,
    IDC_EDT_DURATION,
    IDC_EDT_MAX_VISIBLE,
    IDC_BTN_AUTOSTART,
    IDC_BTN_RESET,
    IDC_BTN_HIDE_TRAY,
    IDC_BTN_WEB_LINK,
    IDC_BTN_DEEP_LINK,
    IDC_BTN_APP_UPDATE
};

namespace {

bool HttpGetBytes(const std::wstring& url, std::vector<uint8_t>& outBytes, DWORD& outStatus) {
    outBytes.clear();
    outStatus = 0;

    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[2048] = {};
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 2047;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }

    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0 && uc.lpszExtraInfo) {
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }

    HINTERNET hSession = WinHttpOpen(L"BorderWatch-Native/4.8",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, 4000, 4000, 8000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort ? uc.nPort : INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    const wchar_t* headers = L"User-Agent: BorderWatch-Native/4.8\r\nAccept: application/vnd.github+json, application/octet-stream, */*\r\n";
    BOOL bResults = WinHttpSendRequest(hRequest, headers, (DWORD)-1L,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    if (bResults) {
        DWORD statusSize = sizeof(outStatus);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &outStatus, &statusSize, WINHTTP_NO_HEADER_INDEX);

        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            size_t oldSize = outBytes.size();
            outBytes.resize(oldSize + dwSize);
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, outBytes.data() + oldSize, dwSize, &dwDownloaded)) {
                outBytes.resize(oldSize + dwDownloaded);
            } else {
                outBytes.resize(oldSize);
                break;
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return bResults == TRUE && !outBytes.empty();
}

std::vector<int> ParseVersionParts(const std::wstring& verStr) {
    std::vector<int> parts;
    std::wstring clean = verStr;
    while (!clean.empty() && (clean.front() == L'v' || clean.front() == L'V' || clean.front() == L' ')) {
        clean.erase(clean.begin());
    }
    std::wstringstream ss(clean);
    std::wstring token;
    while (std::getline(ss, token, L'.')) {
        try {
            parts.push_back(std::stoi(token));
        } catch (...) {
            parts.push_back(0);
        }
    }
    while (parts.size() < 3) parts.push_back(0);
    return parts;
}

bool IsNewerVersion(const std::wstring& remoteVer, const std::wstring& localVer) {
    auto r = ParseVersionParts(remoteVer);
    auto l = ParseVersionParts(localVer);
    for (size_t i = 0; i < std::max(r.size(), l.size()); ++i) {
        int rv = (i < r.size()) ? r[i] : 0;
        int lv = (i < l.size()) ? l[i] : 0;
        if (rv > lv) return true;
        if (rv < lv) return false;
    }
    return false;
}

} // namespace

static WNDPROC s_oldButtonProc = nullptr;
static LRESULT CALLBACK ModernButtonSubclass(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR: {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        if (!GetPropW(hWnd, L"Hover")) {
            SetPropW(hWnd, L"Hover", (HANDLE)1);
            InvalidateRect(hWnd, nullptr, FALSE);
            if (GetDlgCtrlID(hWnd) == IDC_BTN_DEEP_LINK) {
                HWND hParent = GetParent(hWnd);
                if (hParent) SendMessageW(hParent, WM_USER + 250, IDC_BTN_DEEP_LINK, 1);
            }
        }
        break;
    }
    case WM_MOUSELEAVE:
        if (GetDlgCtrlID(hWnd) == IDC_BTN_DEEP_LINK) {
            HWND hParent = GetParent(hWnd);
            if (hParent) SendMessageW(hParent, WM_USER + 250, IDC_BTN_DEEP_LINK, 0);
        }
        RemovePropW(hWnd, L"Hover");
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    return CallWindowProcW(s_oldButtonProc, hWnd, msg, wParam, lParam);
}

static void SubclassButton(HWND hBtn) {
    if (!hBtn) return;
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)ModernButtonSubclass);
    if (!s_oldButtonProc && old) s_oldButtonProc = old;
}

MainWindow::MainWindow(HINSTANCE hInstance, const std::wstring& dataDir)
    : m_hInstance(hInstance), m_dataDir(dataDir) {}

MainWindow::~MainWindow() {
    HotkeyManager::UninstallHook();
    if (m_scanner) m_scanner->Stop();
    if (m_githubThread.joinable()) m_githubThread.join();
    if (m_updateThread.joinable()) m_updateThread.join();
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hCardBrush) DeleteObject(m_hCardBrush);
    if (m_hFontRegular) DeleteObject(m_hFontRegular);
    if (m_hFontBold) DeleteObject(m_hFontBold);
    if (m_hFontTitle) DeleteObject(m_hFontTitle);
    if (m_hFontHeader) DeleteObject(m_hFontHeader);
}

bool MainWindow::Create() {
    Utils::EnsureDirectoryExists(m_dataDir);

    std::wstring oldExePath = Utils::GetExecutablePath() + L".old";
    if (Utils::FileExists(oldExePath)) {
        DeleteFileW(oldExePath.c_str());
    }

    // load config
    std::wstring configPath = Utils::CombinePath(m_dataDir, L"settings.json");
    m_config = Config::LoadFromFile(configPath);

    // load embedded watchlist
    std::wstring playersPath = Utils::CombinePath(m_dataDir, L"players.json");
    m_watchlist.LoadFromFile(playersPath);

    // audio and toast managers
    m_audio = std::make_unique<AudioPlayer>();
    m_audio->Initialize();

    m_toasts = std::make_unique<ToastManager>(m_hInstance);
    m_toasts->Initialize();

    // scanner
    m_scanner = std::make_unique<LogScanner>(m_watchlist, m_config, m_dataDir);

    // reg modern controls
    RegisterModernControls(m_hInstance);

    // reg window class
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = MAIN_CLASS_NAME;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // create main window
    m_hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        MAIN_CLASS_NAME,
        L"BorderWatch 5.0",
        WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 580, 695,
        nullptr, nullptr, m_hInstance, this
    );

    if (!m_hWnd) return false;

    // apply win 11 DWM
    int cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
    MARGINS shadowMargins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(m_hWnd, &shadowMargins);

    // force frame update
    SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // detected window
    m_detectedWnd = std::make_unique<DetectedWindow>(m_hInstance, m_hWnd);
    if (m_toasts) {
        m_toasts->SetParentWindow(m_hWnd);
    }

    // tray
    m_tray = std::make_unique<TrayIcon>(m_hInstance, m_hWnd);
    m_tray->Create();

    // hotkey
    HotkeyManager::InstallHook(m_hInstance, m_hWnd);

    CreateControls();
    SyncControlsFromConfig();
    ApplyTheme();

    // start background scanner thread
    m_scanner->Start();

    // start asynchronous GitHub watchlist update immediately and every 15 minutes automatically
    StartGitHubSync();
    CheckForAppUpdates(false);
    SetTimer(m_hWnd, 2, 15 * 60 * 1000, nullptr);

    // start low-power idle UI timer
    m_fastTimerActive = false;
    SetTimer(m_hWnd, 1, 150, nullptr);

    return true;
}

void MainWindow::EnsureAnimationTimer() {
    if (!m_hWnd || !IsWindow(m_hWnd)) return;
    bool needFast = (m_toasts && m_toasts->HasActiveToasts());
    if (needFast && !m_fastTimerActive) {
        m_fastTimerActive = true;
        SetTimer(m_hWnd, 1, 16, nullptr);
    } else if (!needFast && m_fastTimerActive) {
        m_fastTimerActive = false;
        SetTimer(m_hWnd, 1, 150, nullptr);
    }
}

void MainWindow::SetSaveStatusText(const std::wstring& text, int autoClearMs) {
    if (m_saveStatusText != text) {
        m_saveStatusText = text;
        if (m_hWnd && IsWindow(m_hWnd)) {
            RECT rcClient;
            GetClientRect(m_hWnd, &rcClient);
            RECT rcInvalidate = { 180, 248, rcClient.right - 20, 276 };
            InvalidateRect(m_hWnd, &rcInvalidate, FALSE);
        }
    }
    if (m_hWnd && IsWindow(m_hWnd)) {
        if (!text.empty() && autoClearMs > 0) {
            SetTimer(m_hWnd, 3, (UINT)autoClearMs, nullptr);
        } else {
            KillTimer(m_hWnd, 3);
        }
    }
}

void MainWindow::StartGitHubSync() {
    bool expected = false;
    if (!m_githubSyncInFlight.compare_exchange_strong(expected, true)) {
        return;
    }
    if (m_githubThread.joinable()) {
        m_githubThread.join();
    }
    m_githubText = L"Auto-syncing GitHub watchlist...";
    if (m_hWnd && IsWindow(m_hWnd)) {
        RECT rcInv = { 30, 158, 430, 186 };
        InvalidateRect(m_hWnd, &rcInv, FALSE);
    }

    std::wstring playersPath = Utils::CombinePath(m_dataDir, L"players.json");
    std::wstring customUrl = m_config.watchlist_url;
    HWND targetHwnd = m_hWnd;

    m_githubThread = std::thread([this, playersPath, customUrl, targetHwnd]() {
        std::wstring msg, err;
        m_watchlist.UpdateFromGitHub(playersPath, msg, err, customUrl);
        {
            std::lock_guard<std::mutex> lock(m_githubMutex);
            m_githubStatusMsg = msg + L" (auto-sync 15m)";
            m_githubStatusErr = err;
        }
        m_githubSyncInFlight = false;
        if (IsWindow(targetHwnd)) {
            PostMessageW(targetHwnd, WM_BW_GITHUB_UPDATED, 0, 0);
        }
    });
}

void MainWindow::CheckForAppUpdates(bool manualTrigger) {
    bool expected = false;
    if (!m_updateInFlight.compare_exchange_strong(expected, true)) {
        return;
    }
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }
    if (m_hAppUpdateBtn && IsWindow(m_hAppUpdateBtn)) {
        SetWindowTextW(m_hAppUpdateBtn, L"Checking...");
        InvalidateRect(m_hAppUpdateBtn, nullptr, FALSE);
    }
    if (manualTrigger) {
        SetSaveStatusText(L"Checking GitHub for BorderWatch updates...");
    }

    std::wstring apiUrl = m_config.update_url;
    HWND targetHwnd = m_hWnd;

    m_updateThread = std::thread([this, apiUrl, targetHwnd, manualTrigger]() {
        std::vector<uint8_t> body;
        DWORD status = 0;
        std::wstring foundVersion;
        std::wstring foundDownloadUrl;

        auto parseUpdateJson = [&](const std::vector<uint8_t>& raw) -> bool {
            yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(raw.data()), raw.size(), 0);
            if (!doc) return false;
            yyjson_val* root = yyjson_doc_get_root(doc);
            if (!root || !yyjson_is_obj(root)) {
                yyjson_doc_free(doc);
                return false;
            }
            yyjson_val* vTag = yyjson_obj_get(root, "tag_name");
            if (!vTag) vTag = yyjson_obj_get(root, "version");
            if (vTag && yyjson_is_str(vTag)) {
                foundVersion = Utils::Utf8ToWide(yyjson_get_str(vTag));
            }
            yyjson_val* vDirectUrl = yyjson_obj_get(root, "download_url");
            if (vDirectUrl && yyjson_is_str(vDirectUrl)) {
                foundDownloadUrl = Utils::Utf8ToWide(yyjson_get_str(vDirectUrl));
            }
            yyjson_val* vAssets = yyjson_obj_get(root, "assets");
            if (foundDownloadUrl.empty() && vAssets && yyjson_is_arr(vAssets)) {
                size_t idx, max;
                yyjson_val* asset;
                yyjson_arr_foreach(vAssets, idx, max, asset) {
                    yyjson_val* vName = yyjson_obj_get(asset, "name");
                    yyjson_val* vUrl = yyjson_obj_get(asset, "browser_download_url");
                    if (vName && yyjson_is_str(vName) && vUrl && yyjson_is_str(vUrl)) {
                        std::wstring nameW = Utils::ToLower(Utils::Utf8ToWide(yyjson_get_str(vName)));
                        if (nameW.size() > 4 && nameW.substr(nameW.size() - 4) == L".exe") {
                            foundDownloadUrl = Utils::Utf8ToWide(yyjson_get_str(vUrl));
                            break;
                        }
                    }
                }
            }
            yyjson_doc_free(doc);
            return !foundVersion.empty();
        };

        bool ok = HttpGetBytes(apiUrl, body, status) && status == 200 && parseUpdateJson(body);
        if (!ok) {
            // fallback to raw version.json manifest in repository root
            std::wstring fallbackUrl = L"https://raw.githubusercontent.com/agentyautja/borderpatrol-list/main/version.json";
            ok = HttpGetBytes(fallbackUrl, body, status) && status == 200 && parseUpdateJson(body);
        }

        {
            std::lock_guard<std::mutex> lock(m_githubMutex);
            if (ok && IsNewerVersion(foundVersion, BW_APP_VERSION) && !foundDownloadUrl.empty()) {
                m_updateAvailable = true;
                m_latestVersion = foundVersion;
                m_updateDownloadUrl = foundDownloadUrl;
                m_updateStatusMsg = L"New version " + foundVersion + L" available! Click 'Update' to install.";
            } else if (ok || status == 404) {
                m_updateAvailable = false;
                m_updateStatusMsg = L"\u2713 BorderWatch v" + std::wstring(BW_APP_VERSION) + L" is up to date.";
            } else {
                m_updateAvailable = false;
                m_updateStatusMsg = L"Could not check updates (HTTP " + std::to_wstring(status) + L").";
            }
        }

        m_updateInFlight = false;
        if (IsWindow(targetHwnd)) {
            PostMessageW(targetHwnd, WM_BW_APP_UPDATE_RESULT, manualTrigger ? 1 : 0, 0);
        }
    });
}

void MainWindow::PerformSelfUpdate() {
    bool expected = false;
    if (!m_updateInFlight.compare_exchange_strong(expected, true)) {
        return;
    }
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }
    if (m_hAppUpdateBtn && IsWindow(m_hAppUpdateBtn)) {
        SetWindowTextW(m_hAppUpdateBtn, L"Updating...");
        InvalidateRect(m_hAppUpdateBtn, nullptr, FALSE);
    }
    SetSaveStatusText(L"Downloading update from GitHub...");

    std::wstring dlUrl = m_updateDownloadUrl;
    HWND targetHwnd = m_hWnd;

    m_updateThread = std::thread([this, dlUrl, targetHwnd]() {
        std::vector<uint8_t> exeBytes;
        DWORD status = 0;
        bool ok = HttpGetBytes(dlUrl, exeBytes, status);

        if (!ok || status != 200 || exeBytes.size() < 102400 || exeBytes[0] != 'M' || exeBytes[1] != 'Z') {
            {
                std::lock_guard<std::mutex> lock(m_githubMutex);
                m_updateStatusMsg = L"Update failed: invalid or incomplete .exe download.";
            }
            m_updateInFlight = false;
            if (IsWindow(targetHwnd)) {
                PostMessageW(targetHwnd, WM_BW_APP_UPDATE_RESULT, 1, 0);
            }
            return;
        }

        std::wstring currentExe = Utils::GetExecutablePath();
        std::wstring newExe = currentExe + L".new";
        std::wstring oldExe = currentExe + L".old";

        {
            std::ofstream out(newExe.c_str(), std::ios::binary | std::ios::trunc);
            if (!out) {
                std::lock_guard<std::mutex> lock(m_githubMutex);
                m_updateStatusMsg = L"Update failed: cannot write temporary file.";
                m_updateInFlight = false;
                if (IsWindow(targetHwnd)) PostMessageW(targetHwnd, WM_BW_APP_UPDATE_RESULT, 1, 0);
                return;
            }
            out.write(reinterpret_cast<const char*>(exeBytes.data()), exeBytes.size());
        }

        DeleteFileW(oldExe.c_str());
        if (!MoveFileExW(currentExe.c_str(), oldExe.c_str(), MOVEFILE_REPLACE_EXISTING) ||
            !MoveFileExW(newExe.c_str(), currentExe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            std::lock_guard<std::mutex> lock(m_githubMutex);
            m_updateStatusMsg = L"Update failed: could not replace executable.";
            m_updateInFlight = false;
            if (IsWindow(targetHwnd)) PostMessageW(targetHwnd, WM_BW_APP_UPDATE_RESULT, 1, 0);
            return;
        }

        m_updateInFlight = false;
        if (IsWindow(targetHwnd)) {
            PostMessageW(targetHwnd, WM_BW_APP_UPDATE_RESULT, 2, 0); // 2 = relaunch updated binary
        }
    });
}

void MainWindow::CreateControls() {
    m_hFontRegular = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontBold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontTitle = CreateFontW(-19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontHeader = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // custom top bar controls
    m_hTopMinBtn = CreateWindowExW(0, L"BUTTON", L"\u2013", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    504, 16, 28, 28, m_hWnd, (HMENU)IDC_BTN_TOP_MIN, m_hInstance, nullptr);
    SubclassButton(m_hTopMinBtn);

    m_hTopCloseBtn = CreateWindowExW(0, L"BUTTON", L"\u00D7", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    536, 16, 28, 28, m_hWnd, (HMENU)IDC_BTN_TOP_CLOSE, m_hInstance, nullptr);
    SubclassButton(m_hTopCloseBtn);

    // session card controls
    m_hWebLinkBtn = CreateWindowExW(0, L"BUTTON", L"Web Link", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    348, 88, 92, 28, m_hWnd, (HMENU)IDC_BTN_WEB_LINK, m_hInstance, nullptr);
    SubclassButton(m_hWebLinkBtn);

    m_hDeepLinkBtn = CreateWindowExW(0, L"BUTTON", L"Deep Link", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        446, 88, 92, 28, m_hWnd, (HMENU)IDC_BTN_DEEP_LINK, m_hInstance, nullptr);
    SubclassButton(m_hDeepLinkBtn);

    m_hAppUpdateBtn = CreateWindowExW(0, L"BUTTON", L"Check updates", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        426, 161, 112, 24, m_hWnd, (HMENU)IDC_BTN_APP_UPDATE, m_hInstance, nullptr);
    SubclassButton(m_hAppUpdateBtn);

    // action buttons row
    m_hDetectedBtn = CreateWindowExW(0, L"BUTTON", L"Detected players", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    20, 204, 165, 34, m_hWnd, (HMENU)IDC_BTN_DETECTED, m_hInstance, nullptr);
    SubclassButton(m_hDetectedBtn);

    m_hPauseBtn = CreateWindowExW(0, L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    195, 204, 105, 34, m_hWnd, (HMENU)IDC_BTN_PAUSE, m_hInstance, nullptr);
    SubclassButton(m_hPauseBtn);

    m_hTestSoundBtn = CreateWindowExW(0, L"BUTTON", L"Test alert", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        310, 204, 105, 34, m_hWnd, (HMENU)IDC_BTN_TEST_SOUND, m_hInstance, nullptr);
    SubclassButton(m_hTestSoundBtn);

    m_hQuitBtn = CreateWindowExW(0, L"BUTTON", L"Quit app", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                425, 204, 119, 34, m_hWnd, (HMENU)IDC_BTN_QUIT, m_hInstance, nullptr);
    SubclassButton(m_hQuitBtn);

    // alerts and audio card
    m_hEnemiesCheck = CreateWindowExW(0, BW_TOGGLE_CLASS, L"Enemies", WS_CHILD | WS_VISIBLE,
                                        36, 276, 120, 24, m_hWnd, (HMENU)IDC_CHK_ENEMIES, m_hInstance, nullptr);

    m_hAlliesCheck = CreateWindowExW(0, BW_TOGGLE_CLASS, L"Allies", WS_CHILD | WS_VISIBLE,
                                    175, 276, 120, 24, m_hWnd, (HMENU)IDC_CHK_ALLIES, m_hInstance, nullptr);

    m_hSoundCheck = CreateWindowExW(0, BW_TOGGLE_CLASS, L"Play sound", WS_CHILD | WS_VISIBLE,
                                    315, 276, 140, 24, m_hWnd, (HMENU)IDC_CHK_SOUND, m_hInstance, nullptr);

    std::vector<std::wstring> soundItems = {
        L"Meow (meow-1.mp3)",
        L"Soft ping (ping.wav)",
        L"Softer meow (meow-soft.wav)",
        L"Original meow (meow.wav)",
        L"Custom audio file..."
    };

    // enemy sound picker + preview button
    m_hSoundCombo = CreateWindowExW(0, BW_DROPDOWN_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                    126, 308, 194, 28, m_hWnd, (HMENU)IDC_CMB_SOUND, m_hInstance, nullptr);
    Dropdown_SetItems(m_hSoundCombo, soundItems);

    m_hPreviewEnemyBtn = CreateWindowExW(0, L"BUTTON", L"\u25B6", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        326, 308, 28, 28, m_hWnd, (HMENU)IDC_BTN_PREVIEW_ENEMY, m_hInstance, nullptr);
    SubclassButton(m_hPreviewEnemyBtn);

    m_hChooseSoundBtn = CreateWindowExW(0, L"BUTTON", L"Choose MP3 / WAV", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        360, 308, 138, 28, m_hWnd, (HMENU)IDC_BTN_CHOOSE_SOUND, m_hInstance, nullptr);
    SubclassButton(m_hChooseSoundBtn);

    // ally sound picker +  preview button
    m_hAllySoundCombo = CreateWindowExW(0, BW_DROPDOWN_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                        126, 342, 194, 28, m_hWnd, (HMENU)IDC_CMB_ALLY_SOUND, m_hInstance, nullptr);
    Dropdown_SetItems(m_hAllySoundCombo, soundItems);

    m_hPreviewAllyBtn = CreateWindowExW(0, L"BUTTON", L"\u25B6", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        326, 342, 28, 28, m_hWnd, (HMENU)IDC_BTN_PREVIEW_ALLY, m_hInstance, nullptr);
    SubclassButton(m_hPreviewAllyBtn);

    m_hChooseAllySoundBtn = CreateWindowExW(0, L"BUTTON", L"Choose MP3 / WAV", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            360, 342, 138, 28, m_hWnd, (HMENU)IDC_BTN_CHOOSE_ALLY_SOUND, m_hInstance, nullptr);
    SubclassButton(m_hChooseAllySoundBtn);

    // volume Slider
    m_hVolumeSlider = CreateWindowExW(0, BW_SLIDER_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                        126, 382, 240, 24, m_hWnd, (HMENU)IDC_SLD_VOLUME, m_hInstance, nullptr);

    // appearance n settings card
    m_hDarkModeCheck = CreateWindowExW(0, BW_TOGGLE_CLASS, L"Dark mode", WS_CHILD | WS_VISIBLE,
                                      36, 464, 130, 26, m_hWnd, (HMENU)IDC_CHK_DARK_MODE, m_hInstance, nullptr);

    m_hCornerCombo = CreateWindowExW(0, BW_DROPDOWN_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                        230, 462, 128, 28, m_hWnd, (HMENU)IDC_CMB_CORNER, m_hInstance, nullptr);
    std::vector<std::wstring> cornerItems = {
        L"bottom-right",
        L"top-right",
        L"bottom-left",
        L"top-left"
    };
    Dropdown_SetItems(m_hCornerCombo, cornerItems);

    m_hDurationEdit = CreateWindowExW(0, BW_STEPPER_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                        102, 499, 72, 26, m_hWnd, (HMENU)IDC_EDT_DURATION, m_hInstance, nullptr);
    Stepper_SetRange(m_hDurationEdit, 1, 30, L"s");

    m_hMaxVisibleEdit = CreateWindowExW(0, BW_STEPPER_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                        270, 499, 64, 26, m_hWnd, (HMENU)IDC_EDT_MAX_VISIBLE, m_hInstance, nullptr);
    Stepper_SetRange(m_hMaxVisibleEdit, 1, 20, L"");

    m_hAutostartBtn = CreateWindowExW(0, L"BUTTON", L"Add to autostart", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        348, 498, 182, 28, m_hWnd, (HMENU)IDC_BTN_AUTOSTART, m_hInstance, nullptr);
    SubclassButton(m_hAutostartBtn);

    // diagnostics n history 
    m_hResetBtn = CreateWindowExW(0, L"BUTTON", L"Reset session alerts", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    20, 548, 160, 30, m_hWnd, (HMENU)IDC_BTN_RESET, m_hInstance, nullptr);
    SubclassButton(m_hResetBtn);

    m_hHideTrayBtn = CreateWindowExW(0, L"BUTTON", L"Hide to tray", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    190, 548, 110, 30, m_hWnd, (HMENU)IDC_BTN_HIDE_TRAY, m_hInstance, nullptr);
    SubclassButton(m_hHideTrayBtn);

    m_hHistoryBox = CreateWindowExW(0, BW_HISTORY_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE,
                                    20, 586, 524, 76, m_hWnd, nullptr, m_hInstance, nullptr);

    // tooltips for deep link, web link, sound previews, and app update buttons
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

        ti.uId = (UINT_PTR)m_hDeepLinkBtn;
        ti.lpszText = const_cast<LPWSTR>(L"Copy roblox:// Deep Link.\r\nTip: Press Win + R, paste (Ctrl+V) and hit Enter to join this server directly!");
        SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

        ti.uId = (UINT_PTR)m_hWebLinkBtn;
        ti.lpszText = const_cast<LPWSTR>(L"Copy https:// Web Server Link to open in your browser.");
        SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

        ti.uId = (UINT_PTR)m_hAppUpdateBtn;
        ti.lpszText = const_cast<LPWSTR>(L"Check for new BorderWatch releases and update in 1 click.");
        SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

        ti.uId = (UINT_PTR)m_hPreviewEnemyBtn;
        ti.lpszText = const_cast<LPWSTR>(L"Preview Enemy alert sound & toast notification.");
        SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

        ti.uId = (UINT_PTR)m_hPreviewAllyBtn;
        ti.lpszText = const_cast<LPWSTR>(L"Preview Ally alert sound & toast notification.");
        SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

void MainWindow::DrawWindowBackground(HDC hdc, int width, int height) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    bool isDark = m_config.appearance.dark_mode;
    Gdiplus::Color bgColor = isDark ? Gdiplus::Color(255, 20, 24, 33) : Gdiplus::Color(255, 244, 247, 252);
    Gdiplus::Color cardBg = isDark ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::Color cardBorder = isDark ? Gdiplus::Color(255, 50, 58, 76) : Gdiplus::Color(255, 224, 230, 240);
    Gdiplus::Color titleColor = isDark ? Gdiplus::Color(255, 243, 245, 252) : Gdiplus::Color(255, 32, 37, 52);
    Gdiplus::Color subtitleColor = isDark ? Gdiplus::Color(255, 154, 165, 188) : Gdiplus::Color(255, 126, 136, 158);
    Gdiplus::Color labelColor = isDark ? Gdiplus::Color(255, 205, 214, 232) : Gdiplus::Color(255, 64, 73, 94);

    g.Clear(bgColor);

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::StringFormat sfCenter;
    sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
    sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::StringFormat sfNearCenter;
    sfNearCenter.SetAlignment(Gdiplus::StringAlignmentNear);
    sfNearCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sfNearCenter.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    sfNearCenter.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    // Header: Title + Version Pill + Subtitle

    Gdiplus::RectF badgeRect(20, 15, 38, 38);
    Gdiplus::GraphicsPath badgePath;
    Utils::AddRoundedRect(badgePath, badgeRect, 11.0f);
    Gdiplus::SolidBrush badgeBrush(Gdiplus::Color(255, 85, 117, 239));
    g.FillPath(&badgeBrush, &badgePath);
    Gdiplus::Pen badgeRingPen(Gdiplus::Color(70, 255, 255, 255), 1.0f);
    g.DrawPath(&badgeRingPen, &badgePath);

    Gdiplus::Font bFont(&ff, 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    g.DrawString(L"B", -1, &bFont, badgeRect, &sfCenter, &whiteBrush);

    Gdiplus::Font titleFont(&ff, 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::SolidBrush titleBrush(titleColor);
    g.DrawString(L"BorderWatch", -1, &titleFont, Gdiplus::PointF(68, 12), &titleBrush);

    // version pill
    std::wstring verTag = std::wstring(L"v") + BW_APP_VERSION;
    Gdiplus::Font pillFont(&ff, 7.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::RectF verBounds;
    g.MeasureString(verTag.c_str(), -1, &pillFont, Gdiplus::PointF(0, 0), &verBounds);
    Gdiplus::RectF verPillRect(202.0f, 16.5f, verBounds.Width + 12.0f, 18.0f);
    Gdiplus::GraphicsPath verPillPath;
    Utils::AddRoundedRect(verPillPath, verPillRect, 6.0f);
    Gdiplus::SolidBrush verPillBg(isDark ? Gdiplus::Color(255, 36, 48, 82) : Gdiplus::Color(255, 228, 236, 255));
    Gdiplus::Pen verPillBorder(isDark ? Gdiplus::Color(120, 85, 117, 239) : Gdiplus::Color(110, 85, 117, 239), 1.0f);
    Gdiplus::SolidBrush verPillTxt(isDark ? Gdiplus::Color(255, 138, 168, 255) : Gdiplus::Color(255, 62, 94, 214));
    g.FillPath(&verPillBg, &verPillPath);
    g.DrawPath(&verPillBorder, &verPillPath);
    g.DrawString(verTag.c_str(), -1, &pillFont, verPillRect, &sfCenter, &verPillTxt);

    Gdiplus::Font subtitleFont(&ff, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::SolidBrush subBrush(subtitleColor);
    g.DrawString(L"A Universal Time  \u00B7  Tab + I to toggle  \u00B7  15m auto-sync", -1, &subtitleFont, Gdiplus::PointF(68, 36), &subBrush);

    // cards with section accent dots and headers

    auto drawCard = [&](float x, float y, float w, float h, const wchar_t* tag, Gdiplus::Color dotColor) {
        Gdiplus::RectF cr(x, y, w, h);
        Gdiplus::GraphicsPath cp;
        Utils::AddRoundedRect(cp, cr, 14.0f);

        Gdiplus::SolidBrush cb(cardBg);
        g.FillPath(&cb, &cp);

        Gdiplus::Pen cPen(cardBorder, 1.0f);
        g.DrawPath(&cPen, &cp);

        if (tag && wcslen(tag) > 0) {
            Gdiplus::SolidBrush dotBrush(dotColor);
            g.FillEllipse(&dotBrush, x + 16.0f, y + 12.5f, 6.0f, 6.0f);

            Gdiplus::Font tagFont(&ff, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush tagBrush(subtitleColor);
            g.DrawString(tag, -1, &tagFont, Gdiplus::PointF(x + 27.0f, y + 9.0f), &tagBrush);
        }
    };

    float cardW = (float)(width - 40);
    bool isPaused = (m_scanner && m_scanner->IsPaused());
    bool isActiveAut = (!isPaused && m_lastStatusText.find(L"Watching") != std::wstring::npos);
    Gdiplus::Color sessionDotCol = isPaused
        ? Gdiplus::Color(255, 245, 166, 35)
        : (isActiveAut ? Gdiplus::Color(255, 48, 164, 108) : Gdiplus::Color(255, 85, 117, 239));

    drawCard(20, 64, cardW, 128, L"LIVE SESSION", sessionDotCol);
    drawCard(20, 246, cardW, 178, L"ALERTS & AUDIO", Gdiplus::Color(255, 229, 72, 77));
    drawCard(20, 434, cardW, 104, L"APPEARANCE & OVERLAY", Gdiplus::Color(255, 85, 117, 239));


    // status pill + status headline + metadata rows

    {
        std::wstring stateLabel = isPaused ? L"\u25CF PAUSED" : (isActiveAut ? L"\u25CF ACTIVE" : L"\u25CF STANDBY");
        Gdiplus::Color sPillBg, sPillBorder, sPillTxt;
        if (isPaused) {
            sPillBg = isDark ? Gdiplus::Color(255, 38, 32, 22) : Gdiplus::Color(255, 253, 248, 240);
            sPillBorder = isDark ? Gdiplus::Color(255, 68, 54, 32) : Gdiplus::Color(255, 244, 230, 210);
            sPillTxt = isDark ? Gdiplus::Color(255, 230, 175, 80) : Gdiplus::Color(255, 175, 110, 25);
        } else if (isActiveAut) {

            sPillBg = isDark ? Gdiplus::Color(255, 25, 36, 30) : Gdiplus::Color(255, 244, 248, 245);
            sPillBorder = isDark ? Gdiplus::Color(255, 42, 62, 50) : Gdiplus::Color(255, 220, 234, 225);
            sPillTxt = isDark ? Gdiplus::Color(255, 120, 200, 155) : Gdiplus::Color(255, 52, 126, 85);
        } else {
            sPillBg = isDark ? Gdiplus::Color(255, 30, 36, 46) : Gdiplus::Color(255, 245, 247, 251);
            sPillBorder = isDark ? Gdiplus::Color(255, 48, 58, 76) : Gdiplus::Color(255, 228, 234, 244);
            sPillTxt = isDark ? Gdiplus::Color(255, 145, 165, 190) : Gdiplus::Color(255, 95, 115, 140);
        }

        Gdiplus::RectF sBounds;
        g.MeasureString(stateLabel.c_str(), -1, &pillFont, Gdiplus::PointF(0, 0), &sBounds);
        Gdiplus::RectF sPillRect(128.0f, 71.0f, sBounds.Width + 12.0f, 17.0f);
        Gdiplus::GraphicsPath sPillPath;
        Utils::AddRoundedRect(sPillPath, sPillRect, 5.5f);
        Gdiplus::SolidBrush sbBrush(sPillBg);
        Gdiplus::Pen sbPen(sPillBorder, 1.0f);
        Gdiplus::SolidBrush stBrush(sPillTxt);
        g.FillPath(&sbBrush, &sPillPath);
        g.DrawPath(&sbPen, &sPillPath);
        g.DrawString(stateLabel.c_str(), -1, &pillFont, sPillRect, &sfCenter, &stBrush);

        // main status headline
        Gdiplus::Font statusHeadlineFont(&ff, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        float maxHeadlineW = (float)std::max(100, width - 256);
        g.DrawString(m_lastStatusText.c_str(), -1, &statusHeadlineFont,
                        Gdiplus::RectF(36.0f, 89.0f, maxHeadlineW, 28.0f), &sfNearCenter, &titleBrush);

        // subtle divider line inside session card
        Gdiplus::Pen divPen(isDark ? Gdiplus::Color(110, 48, 56, 74) : Gdiplus::Color(140, 232, 236, 245), 1.0f);
        g.DrawLine(&divPen, 36.0f, 120.0f, (float)(width - 36), 120.0f);

        Gdiplus::Font metaFont(&ff, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        auto drawMetaRow = [&](float rowY, const std::wstring& txt, float maxW, Gdiplus::Color bulletCol) {
            if (txt.empty()) return;
            Gdiplus::SolidBrush bDot(bulletCol);
            g.FillEllipse(&bDot, 37.0f, rowY + 6.5f, 4.5f, 4.5f);
            g.DrawString(txt.c_str(), -1, &metaFont, Gdiplus::RectF(48.0f, rowY, maxW, 18.0f), &sfNearCenter, &subBrush);
        };

        drawMetaRow(124.0f, m_lastStatsText, cardW - 36.0f, Gdiplus::Color(255, 85, 117, 239));
        drawMetaRow(144.0f, m_lastCoverageText.empty() ? L"No active Roblox log file yet." : m_lastCoverageText,
                    cardW - 36.0f, Gdiplus::Color(255, 135, 148, 175));
        drawMetaRow(164.0f, m_githubText, (float)std::max(100, width - 190), Gdiplus::Color(255, 48, 164, 108));
    }

    // auto save badge + audio form labels + volume %
    
    {
        if (!m_saveStatusText.empty()) {
            bool isErr = (m_saveStatusText.find(L"error") != std::wstring::npos ||
                            m_saveStatusText.find(L"Failed") != std::wstring::npos ||
                            m_saveStatusText.find(L"Could not") != std::wstring::npos ||
                            m_saveStatusText.find(L"No active") != std::wstring::npos);
            bool isTip = (m_saveStatusText.find(L"Tip:") != std::wstring::npos ||
                            m_saveStatusText.find(L"Checking") != std::wstring::npos ||
                            m_saveStatusText.find(L"Downloading") != std::wstring::npos ||
                            m_saveStatusText.find(L"New version") != std::wstring::npos);

            Gdiplus::Color pBg, pTxt, pBorder;
            if (isErr) {
                pBg = isDark ? Gdiplus::Color(255, 45, 26, 32) : Gdiplus::Color(255, 255, 240, 242);
                pTxt = isDark ? Gdiplus::Color(255, 255, 130, 135) : Gdiplus::Color(255, 195, 45, 55);
                pBorder = isDark ? Gdiplus::Color(255, 75, 40, 48) : Gdiplus::Color(255, 248, 215, 220);
            } else if (isTip) {
                pBg = isDark ? Gdiplus::Color(255, 28, 38, 55) : Gdiplus::Color(255, 242, 246, 254);
                pTxt = isDark ? Gdiplus::Color(255, 140, 175, 245) : Gdiplus::Color(255, 60, 95, 205);
                pBorder = isDark ? Gdiplus::Color(255, 48, 65, 95) : Gdiplus::Color(255, 218, 228, 248);
            } else {
                pBg = isDark ? Gdiplus::Color(255, 25, 34, 29) : Gdiplus::Color(255, 245, 248, 246);
                pBorder = isDark ? Gdiplus::Color(255, 38, 56, 44) : Gdiplus::Color(255, 222, 235, 226);
                pTxt = isDark ? Gdiplus::Color(255, 120, 195, 150) : Gdiplus::Color(255, 58, 122, 86);
            }

            Gdiplus::Font saveFont(&ff, 8.2f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::RectF sbBounds;
            g.MeasureString(m_saveStatusText.c_str(), -1, &saveFont, Gdiplus::PointF(0, 0), &sbBounds);
            float pillW = std::min((float)(width - 210), sbBounds.Width + 14.0f);
            float pillX = (float)(width - 20 - 16) - pillW;
            Gdiplus::RectF savePillRect(pillX, 254.0f, pillW, 18.0f);
            Gdiplus::GraphicsPath savePillPath;
            Utils::AddRoundedRect(savePillPath, savePillRect, 5.0f);
            Gdiplus::SolidBrush spBrush(pBg);
            Gdiplus::Pen spPen(pBorder, 1.0f);
            Gdiplus::SolidBrush spTxtBrush(pTxt);
            g.FillPath(&spBrush, &savePillPath);
            g.DrawPath(&spPen, &savePillPath);
            g.DrawString(m_saveStatusText.c_str(), -1, &saveFont, savePillRect, &sfCenter, &spTxtBrush);
        }

        Gdiplus::Font formFont(&ff, 9.2f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush lblBrush(labelColor);

        g.DrawString(L"Enemy sound:", -1, &formFont, Gdiplus::RectF(36.0f, 308.0f, 88.0f, 28.0f), &sfNearCenter, &lblBrush);
        g.DrawString(L"Ally sound:", -1, &formFont, Gdiplus::RectF(36.0f, 342.0f, 88.0f, 28.0f), &sfNearCenter, &lblBrush);
        g.DrawString(L"Volume:", -1, &formFont, Gdiplus::RectF(36.0f, 380.0f, 88.0f, 26.0f), &sfNearCenter, &lblBrush);
    }


    // appearance and overlay form labels

    {
        Gdiplus::Font formFont(&ff, 9.2f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush lblBrush(labelColor);

        g.DrawString(L"Corner:", -1, &formFont, Gdiplus::RectF(175.0f, 462.0f, 52.0f, 28.0f), &sfNearCenter, &lblBrush);
        g.DrawString(L"Duration:", -1, &formFont, Gdiplus::RectF(36.0f, 498.0f, 64.0f, 26.0f), &sfNearCenter, &lblBrush);
        g.DrawString(L"Max visible:", -1, &formFont, Gdiplus::RectF(190.0f, 498.0f, 78.0f, 26.0f), &sfNearCenter, &lblBrush);
    }


    // session history

    {
        std::wstring countLabel = std::to_wstring(m_historyLines.size()) +
                                    (m_historyLines.size() == 1 ? L" alert logged" : L" alerts logged");
        Gdiplus::Font cntFont(&ff, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::RectF cntBounds;
        g.MeasureString(countLabel.c_str(), -1, &cntFont, Gdiplus::PointF(0, 0), &cntBounds);
        float cPillW = cntBounds.Width + 16.0f;
        Gdiplus::RectF cntRect((float)(width - 20) - cPillW, 553.0f, cPillW, 20.0f);
        Gdiplus::GraphicsPath cntPath;
        Utils::AddRoundedRect(cntPath, cntRect, 6.0f);
        Gdiplus::SolidBrush cntBg(isDark ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 234, 239, 248));
        Gdiplus::Pen cntPen(cardBorder, 1.0f);
        g.FillPath(&cntBg, &cntPath);
        g.DrawPath(&cntPen, &cntPath);
        g.DrawString(countLabel.c_str(), -1, &cntFont, cntRect, &sfCenter, &subBrush);
    }
}

void MainWindow::RelayoutControls(int width, int height) {
    int cardW = width - 40;

    // top bar buttons
    if (m_hTopCloseBtn && IsWindow(m_hTopCloseBtn)) {
        SetWindowPos(m_hTopCloseBtn, nullptr, width - 44, 16, 28, 28, SWP_NOZORDER);
    }
    if (m_hTopMinBtn && IsWindow(m_hTopMinBtn)) {
        SetWindowPos(m_hTopMinBtn, nullptr, width - 76, 16, 28, 28, SWP_NOZORDER);
    }

    // session card controls
    if (m_hDeepLinkBtn && IsWindow(m_hDeepLinkBtn)) {
        SetWindowPos(m_hDeepLinkBtn, nullptr, width - 20 - 16 - 92, 88, 92, 28, SWP_NOZORDER);
    }
    if (m_hWebLinkBtn && IsWindow(m_hWebLinkBtn)) {
        SetWindowPos(m_hWebLinkBtn, nullptr, width - 20 - 16 - 92 - 6 - 92, 88, 92, 28, SWP_NOZORDER);
    }
    if (m_hAppUpdateBtn && IsWindow(m_hAppUpdateBtn)) {
        SetWindowPos(m_hAppUpdateBtn, nullptr, width - 20 - 16 - 112, 161, 112, 24, SWP_NOZORDER);
    }

    // action buttons row
    int gap = 8;
    int totalGaps = gap * 3;
    int availW = cardW - totalGaps;
    int wDetected = std::max(150, availW * 34 / 100);
    int wPause = std::max(90, availW * 20 / 100);
    int wTest = std::max(95, availW * 22 / 100);
    int wQuit = availW - wDetected - wPause - wTest;
    int x1 = 20;
    int x2 = x1 + wDetected + gap;
    int x3 = x2 + wPause + gap;
    int x4 = x3 + wTest + gap;

    if (m_hDetectedBtn && IsWindow(m_hDetectedBtn)) SetWindowPos(m_hDetectedBtn, nullptr, x1, 204, wDetected, 34, SWP_NOZORDER);
    if (m_hPauseBtn && IsWindow(m_hPauseBtn)) SetWindowPos(m_hPauseBtn, nullptr, x2, 204, wPause, 34, SWP_NOZORDER);
    if (m_hTestSoundBtn && IsWindow(m_hTestSoundBtn)) SetWindowPos(m_hTestSoundBtn, nullptr, x3, 204, wTest, 34, SWP_NOZORDER);
    if (m_hQuitBtn && IsWindow(m_hQuitBtn)) SetWindowPos(m_hQuitBtn, nullptr, x4, 204, wQuit, 34, SWP_NOZORDER);

    // appearance and settings card
    if (m_hAutostartBtn && IsWindow(m_hAutostartBtn)) {
        SetWindowPos(m_hAutostartBtn, nullptr, width - 20 - 16 - 182, 498, 182, 28, SWP_NOZORDER);
    }

    // diagnostics and history box
    int histH = std::max(60, height - 586 - 18);
    if (m_hHistoryBox && IsWindow(m_hHistoryBox)) {
        SetWindowPos(m_hHistoryBox, nullptr, 20, 586, cardW, histH, SWP_NOZORDER);
        InvalidateRect(m_hHistoryBox, nullptr, FALSE);
    }

    InvalidateRect(m_hWnd, nullptr, TRUE);
}

void MainWindow::DrawModernButton(LPDRAWITEMSTRUCT dis) {
    int w = dis->rcItem.right - dis->rcItem.left;
    int h = dis->rcItem.bottom - dis->rcItem.top;

    HDC hdcMem = CreateCompatibleDC(dis->hDC);
    HBITMAP hbm = CreateCompatibleBitmap(dis->hDC, w, h);
    HGDIOBJ oldBm = SelectObject(hdcMem, hbm);

    {
        Gdiplus::Graphics g(hdcMem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        int id = (int)dis->CtlID;
        bool isPressed = (dis->itemState & ODS_SELECTED);
        bool isHovered = (GetPropW(dis->hwndItem, L"Hover") != nullptr);
        bool isDark = m_config.appearance.dark_mode;

        bool onCard = (id == IDC_BTN_SAVE || id == IDC_BTN_CHOOSE_SOUND || id == IDC_BTN_CHOOSE_ALLY_SOUND ||
                        id == IDC_BTN_PREVIEW_ENEMY || id == IDC_BTN_PREVIEW_ALLY ||
                        id == IDC_BTN_AUTOSTART || id == IDC_BTN_WEB_LINK || id == IDC_BTN_DEEP_LINK || id == IDC_BTN_APP_UPDATE);
        Gdiplus::Color underBg = onCard ? (isDark ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 255, 255, 255))
                                        : (isDark ? Gdiplus::Color(255, 20, 24, 33) : Gdiplus::Color(255, 244, 247, 252));
        g.Clear(underBg);

        Gdiplus::RectF rect(0, 0, (float)w, (float)h);
        Gdiplus::GraphicsPath path;

        Gdiplus::Color bgColor, textColor, borderColor;
        bool hasBorder = false;

        if (id == IDC_BTN_TOP_MIN || id == IDC_BTN_TOP_CLOSE) {
            Utils::AddRoundedRect(path, rect, 7.0f);
            if (id == IDC_BTN_TOP_CLOSE) {
                if (isHovered || isPressed) {
                    bgColor = isDark ? (isPressed ? Gdiplus::Color(255, 110, 40, 52) : Gdiplus::Color(255, 235, 72, 77))
                                        : (isPressed ? Gdiplus::Color(255, 220, 55, 60) : Gdiplus::Color(255, 235, 72, 77));
                    textColor = Gdiplus::Color(255, 255, 255, 255);
                } else {
                    bgColor = underBg;
                    textColor = isDark ? Gdiplus::Color(255, 161, 172, 194) : Gdiplus::Color(255, 126, 139, 159);
                }
            } else { // IDC_BTN_TOP_MIN
                if (isHovered || isPressed) {
                    bgColor = isDark ? (isPressed ? Gdiplus::Color(255, 48, 55, 72) : Gdiplus::Color(255, 40, 46, 60))
                                        : (isPressed ? Gdiplus::Color(255, 218, 224, 236) : Gdiplus::Color(255, 232, 236, 244));
                    textColor = isDark ? Gdiplus::Color(255, 241, 243, 250) : Gdiplus::Color(255, 36, 41, 56);
                } else {
                    bgColor = underBg;
                    textColor = isDark ? Gdiplus::Color(255, 161, 172, 194) : Gdiplus::Color(255, 126, 139, 159);
                }
            }
            hasBorder = false;
        } else {
            Utils::AddRoundedRect(path, rect, 10.0f);
            if (id == IDC_BTN_DETECTED || id == IDC_BTN_SAVE || (id == IDC_BTN_APP_UPDATE && m_updateAvailable)) {
                if (isPressed) bgColor = Gdiplus::Color(255, 66, 97, 220);       // #4261DC
                else if (isHovered) bgColor = Gdiplus::Color(255, 107, 136, 245); // #6B88F5
                else bgColor = Gdiplus::Color(255, 85, 117, 239);                // #5575EF
                textColor = Gdiplus::Color(255, 255, 255, 255);
            } else if (id == IDC_BTN_QUIT) {

                hasBorder = true;
                if (isDark) {
                    if (isPressed) {
                        bgColor = Gdiplus::Color(255, 80, 45, 58);
                        borderColor = Gdiplus::Color(255, 110, 55, 70);
                        textColor = Gdiplus::Color(255, 255, 165, 175);
                    } else if (isHovered) {
                        bgColor = Gdiplus::Color(255, 68, 44, 56);
                        borderColor = Gdiplus::Color(255, 95, 55, 70);
                        textColor = Gdiplus::Color(255, 255, 150, 165);
                    } else {
                        bgColor = Gdiplus::Color(255, 48, 55, 72);
                        borderColor = Gdiplus::Color(255, 57, 66, 85);
                        textColor = Gdiplus::Color(255, 223, 229, 242);
                    }
                } else {
                    if (isPressed) {
                        bgColor = Gdiplus::Color(255, 246, 218, 224);
                        borderColor = Gdiplus::Color(255, 235, 195, 205);
                        textColor = Gdiplus::Color(255, 195, 40, 58);
                    } else if (isHovered) {
                        bgColor = Gdiplus::Color(255, 253, 238, 242);
                        borderColor = Gdiplus::Color(255, 240, 212, 220);
                        textColor = Gdiplus::Color(255, 205, 48, 65);
                    } else {
                        bgColor = Gdiplus::Color(255, 237, 240, 246);
                        borderColor = Gdiplus::Color(255, 220, 225, 236);
                        textColor = Gdiplus::Color(255, 55, 64, 86);
                    }
                }
            } else {
                // secondary pill
                if (isDark) {
                    bgColor = isPressed ? Gdiplus::Color(255, 38, 44, 58) : (isHovered ? Gdiplus::Color(255, 57, 66, 85) : Gdiplus::Color(255, 48, 55, 72));
                    textColor = Gdiplus::Color(255, 223, 229, 242);
                    borderColor = Gdiplus::Color(255, 57, 66, 85);
                } else {
                    bgColor = isPressed ? Gdiplus::Color(255, 226, 230, 239) : (isHovered ? Gdiplus::Color(255, 242, 245, 250) : Gdiplus::Color(255, 237, 240, 246));
                    textColor = Gdiplus::Color(255, 55, 64, 86);
                    borderColor = Gdiplus::Color(255, 220, 225, 236);
                }
                hasBorder = true;
            }
        }

        Gdiplus::SolidBrush bgBrush(bgColor);
        g.FillPath(&bgBrush, &path);

        if (hasBorder) {
            Gdiplus::Pen borderPen(borderColor, 1.0f);
            g.DrawPath(&borderPen, &path);
        }

        wchar_t text[128] = { 0 };
        GetWindowTextW(dis->hwndItem, text, 128);

        Gdiplus::FontFamily ff(L"Segoe UI");
        bool isTopBtn = (id == IDC_BTN_TOP_MIN || id == IDC_BTN_TOP_CLOSE);
        bool isBold = (id == IDC_BTN_DETECTED || id == IDC_BTN_SAVE || id == IDC_BTN_PAUSE ||
                        id == IDC_BTN_TEST_SOUND || id == IDC_BTN_QUIT ||
                        (id == IDC_BTN_APP_UPDATE && m_updateAvailable) || isTopBtn);
        float fontSize = isTopBtn ? 12.5f : (id == IDC_BTN_APP_UPDATE ? 8.5f : 9.5f);
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
}

void MainWindow::SyncControlsFromConfig() {
    Toggle_SetChecked(m_hEnemiesCheck, m_config.notify_enemies);
    Toggle_SetChecked(m_hAlliesCheck, m_config.notify_allies);
    Toggle_SetChecked(m_hSoundCheck, m_config.sound.enabled);
    Toggle_SetChecked(m_hDarkModeCheck, m_config.appearance.dark_mode);

    auto mapSoundIndex = [](const std::wstring& s) -> int {
        if (s == L"meow-1.mp3") return 0;
        if (s == L"ping.wav") return 1;
        if (s == L"meow-soft.wav") return 2;
        if (s == L"meow.wav") return 3;
        return 4;
    };

    Dropdown_SetCurSel(m_hSoundCombo, mapSoundIndex(m_config.sound.enemy_file));
    Dropdown_SetCurSel(m_hAllySoundCombo, mapSoundIndex(m_config.sound.ally_file));

    Slider_SetValue(m_hVolumeSlider, m_config.sound.volume);

    int cornerIdx = 0;
    if (m_config.notification.corner == L"top-right") cornerIdx = 1;
    else if (m_config.notification.corner == L"bottom-left") cornerIdx = 2;
    else if (m_config.notification.corner == L"top-left") cornerIdx = 3;
    Dropdown_SetCurSel(m_hCornerCombo, cornerIdx);

    Stepper_SetValue(m_hDurationEdit, (int)m_config.notification.duration_seconds);
    Stepper_SetValue(m_hMaxVisibleEdit, m_config.notification.max_visible);

    bool autostart = StartupManager::IsAutostartEnabled();
    SetWindowTextW(m_hAutostartBtn, autostart ? L"Remove from autostart" : L"Add to autostart");
}

void MainWindow::ApplyTheme() {
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hCardBrush) DeleteObject(m_hCardBrush);

    ModernControls_SetDarkMode(m_config.appearance.dark_mode);

    if (m_config.appearance.dark_mode) {
        m_hBgBrush = CreateSolidBrush(RGB(23, 27, 36));
        m_hCardBrush = CreateSolidBrush(RGB(34, 39, 52));
    } else {
        m_hBgBrush = CreateSolidBrush(RGB(245, 247, 252));
        m_hCardBrush = CreateSolidBrush(RGB(255, 255, 255));
    }

    RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    if (m_detectedWnd) {
        m_detectedWnd->ApplyTheme(m_config);
    }
}

void MainWindow::SaveConfigFromControls() {
    bool prevDark = m_config.appearance.dark_mode;
    m_config.notify_enemies = Toggle_GetChecked(m_hEnemiesCheck);
    m_config.notify_allies = Toggle_GetChecked(m_hAlliesCheck);
    m_config.sound.enabled = Toggle_GetChecked(m_hSoundCheck);
    m_config.appearance.dark_mode = Toggle_GetChecked(m_hDarkModeCheck);

    int soundIdx = Dropdown_GetCurSel(m_hSoundCombo);
    if (soundIdx == 0) m_config.sound.enemy_file = L"meow-1.mp3";
    else if (soundIdx == 1) m_config.sound.enemy_file = L"ping.wav";
    else if (soundIdx == 2) m_config.sound.enemy_file = L"meow-soft.wav";
    else if (soundIdx == 3) m_config.sound.enemy_file = L"meow.wav";
    m_config.sound.file = m_config.sound.enemy_file;

    int allySoundIdx = Dropdown_GetCurSel(m_hAllySoundCombo);
    if (allySoundIdx == 0) m_config.sound.ally_file = L"meow-1.mp3";
    else if (allySoundIdx == 1) m_config.sound.ally_file = L"ping.wav";
    else if (allySoundIdx == 2) m_config.sound.ally_file = L"meow-soft.wav";
    else if (allySoundIdx == 3) m_config.sound.ally_file = L"meow.wav";

    m_config.sound.volume = Slider_GetValue(m_hVolumeSlider);

    int cornerIdx = Dropdown_GetCurSel(m_hCornerCombo);
    if (cornerIdx == 1) m_config.notification.corner = L"top-right";
    else if (cornerIdx == 2) m_config.notification.corner = L"bottom-left";
    else if (cornerIdx == 3) m_config.notification.corner = L"top-left";
    else m_config.notification.corner = L"bottom-right";

    m_config.notification.duration_seconds = (double)Stepper_GetValue(m_hDurationEdit);
    m_config.notification.max_visible = Stepper_GetValue(m_hMaxVisibleEdit);

    std::wstring configPath = Utils::CombinePath(m_dataDir, L"settings.json");
    if (m_config.SaveToFile(configPath)) {
        SetSaveStatusText(L"\u2713 Auto-saved");
        m_scanner->UpdateConfig(m_config);
        if (prevDark != m_config.appearance.dark_mode) {
            ApplyTheme();
        } else if (m_hWnd && IsWindow(m_hWnd)) {
            RECT rcVol = { 368, 378, 430, 410 };
            InvalidateRect(m_hWnd, &rcVol, FALSE);
        }
    } else {
        SetSaveStatusText(L"Failed to save preferences.");
    }
}

void MainWindow::ChooseCustomSound(bool isAlly) {
    wchar_t filename[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"Audio files (*.mp3;*.wav)\0*.mp3;*.wav\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring err;
        std::wstring imported = m_audio->ImportCustomSound(filename, m_dataDir, err);
        if (!imported.empty()) {
            if (isAlly) {
                m_config.sound.ally_file = imported;
                Dropdown_SetCurSel(m_hAllySoundCombo, 4);
                SaveConfigFromControls();
                SetSaveStatusText(L"\u2713 Custom Ally sound saved");
            } else {
                m_config.sound.enemy_file = imported;
                m_config.sound.file = imported;
                Dropdown_SetCurSel(m_hSoundCombo, 4);
                SaveConfigFromControls();
                SetSaveStatusText(L"\u2713 Custom Enemy sound saved");
            }
        } else {
            SetSaveStatusText(L"Audio error: " + err);
        }
    }
}

void MainWindow::ProcessScannerEvents() {
    auto events = m_scanner->DequeueEvents();
    if (events.empty()) return;

    for (const auto& ev : events) {
        if (m_config.sound.enabled) {
            bool play = (!m_config.sound.enemies_only || ev.status == L"Enemy");
            if (play) {
                const std::wstring& sndFile = (ev.status == L"Ally")
                    ? m_config.sound.ally_file
                    : m_config.sound.enemy_file;
                m_audio->Play(sndFile, m_config.sound.volume, m_dataDir);
            }
        }
        m_toasts->ShowToast(ev.username, ev.status, ev.clan, ev.is_summary, m_config, ev.avatar_png);
        EnsureAnimationTimer();

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t timeBuf[32];
        swprintf_s(timeBuf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

        std::wstring line = std::wstring(timeBuf) + L"  " + ev.username + L" / " + ev.status + L" / " + ev.clan;
        m_historyLines.insert(m_historyLines.begin(), line);
        if (m_historyLines.size() > 50) m_historyLines.pop_back();

        History_SetLines(m_hHistoryBox, m_historyLines);
    }

    if (m_hWnd && IsWindow(m_hWnd)) {
        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);
        RECT rcCnt = { rcClient.right - 180, 546, rcClient.right - 16, 580 };
        InvalidateRect(m_hWnd, &rcCnt, FALSE);
    }
}

void MainWindow::UpdateStatusDisplay() {
    bool changed = false;

    std::wstring status = m_scanner->GetStatus();
    if (status != m_lastStatusText) {
        m_lastStatusText = status;
        changed = true;
    }

    std::wstring stats = std::to_wstring(m_watchlist.Count()) + L" members loaded  \u00B7  Last scan " + m_scanner->GetLastScanTime();
    if (stats != m_lastStatsText) {
        m_lastStatsText = stats;
        changed = true;
    }

    std::wstring coverage = m_scanner->GetCoverage();
    if (coverage != m_lastCoverageText) {
        m_lastCoverageText = coverage;
        changed = true;
    }

    if (changed && m_hWnd && IsWindow(m_hWnd)) {
        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);
        RECT rcSession = { 20, 64, rcClient.right - 20, 192 };
        InvalidateRect(m_hWnd, &rcSession, FALSE);
    }
}

void MainWindow::CopyServerWebLink() {
    std::wstring link = m_scanner ? m_scanner->GetServerWebLink() : L"";
    if (!link.empty()) {
        Utils::CopyTextToClipboard(m_hWnd, link);
        SetSaveStatusText(L"\u2713 Web link (https://) copied!");
        if (m_toasts) {
            m_toasts->ShowToast(L"Web Link Copied", L"Ally", L"Roblox Web Server Link", false, m_config);
            EnsureAnimationTimer();
        }
    } else {
        SetSaveStatusText(L"No active server detected yet.");
    }
}

void MainWindow::CopyServerDeepLink() {
    std::wstring link = m_scanner ? m_scanner->GetServerDeepLink() : L"";
    if (!link.empty()) {
        Utils::CopyTextToClipboard(m_hWnd, link);
        SetSaveStatusText(L"\u2713 Copied! Win+R \u2192 Ctrl+V \u2192 Enter");
        if (m_toasts) {
            m_toasts->ShowToast(L"Server Link Copied", L"Ally", L"Press Win + R, paste & Enter", false, m_config);
            EnsureAnimationTimer();
        }
    } else {
        SetSaveStatusText(L"No active server detected yet.");
    }
}

void MainWindow::ShowPanel() {
    ShowWindow(m_hWnd, SW_RESTORE);
    SetForegroundWindow(m_hWnd);
}

void MainWindow::HidePanel() {
    ShowWindow(m_hWnd, SW_HIDE);
    if (m_detectedWnd) m_detectedWnd->Hide();
}

void MainWindow::ToggleVisibility() {
    if (IsWindowVisible(m_hWnd) && !IsIconic(m_hWnd)) {
        HidePanel();
    } else {
        ShowPanel();
    }
}

void MainWindow::Run(bool startHidden, bool preview) {
    if (!startHidden) {
        ShowPanel();
    } else {
        HidePanel();
    }

    if (preview) {
        m_audio->Play(m_config.sound.enemy_file, m_config.sound.volume, m_dataDir);
        m_toasts->ShowToast(L"PreviewPlayer", L"Enemy", L"Preview Clan", false, m_config);
        EnsureAnimationTimer();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        return self->HandleMessage(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right - rcClient.left;
        int h = rcClient.bottom - rcClient.top;

        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdcScreen, w, h);
        HGDIOBJ hOldBm = SelectObject(hdcMem, hbmMem);

        DrawWindowBackground(hdcMem, w, h);

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hOldBm);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        return 0;
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
        if (w > 0 && h > 0) {
            RelayoutControls(w, h);
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 560;
        mmi->ptMinTrackSize.y = 660;
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        // rsizing edges
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

        // allow top right buttons to receive their normal clicks
        if (pt.y >= 10 && pt.y <= 48 && pt.x >= clientW - 85) {
            return HTCLIENT;
        }

        if (pt.y <= 58) {
            return HTCAPTION;
        }

        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        if (y <= 58 && x < 495) {
            ReleaseCapture();
            SendMessageW(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        break;
    }
    case WM_SYSCOMMAND: {
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hWnd, SW_MINIMIZE);
            return 0;
        }
        if ((wParam & 0xFFF0) == SC_RESTORE) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis && dis->CtlType == ODT_BUTTON) {
            DrawModernButton(dis);
            return TRUE;
        }
        break;
    }
    case WM_USER + 250: {
        int ctrlId = (int)wParam;
        bool hovered = (lParam != 0);
        if (ctrlId == IDC_BTN_DEEP_LINK) {
            if (hovered) {
                SetSaveStatusText(L"Tip: Win + R \u2192 Ctrl+V \u2192 Enter", 0);
            } else if (m_saveStatusText.find(L"Tip: Win + R") == 0) {
                SetSaveStatusText(L"", 0);
            }
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 1) {
            static int uiTick = 0;
            bool doUiSync = !m_fastTimerActive || (++uiTick % 6 == 0);
            if (doUiSync) {
                ProcessScannerEvents();
                UpdateStatusDisplay();

                if (m_detectedWnd && m_detectedWnd->IsVisible()) {
                    auto players = m_scanner->GetDetectedPlayers();
                    int enemies = 0, allies = 0;
                    for (const auto& p : players) {
                        if (p.status == L"Enemy") enemies++;
                        else allies++;
                    }
                    std::wstring summary = std::to_wstring(enemies) + L" enemies / " + std::to_wstring(allies) +
                                            L" allies detected in this session's log. Updated " + m_scanner->GetLastScanTime() + L".";
                    m_detectedWnd->UpdateData(players, summary, m_config, m_scanner->GetServerWebLink(), m_scanner->GetServerDeepLink());
                }
            }
            if (m_toasts && m_toasts->HasActiveToasts()) {
                m_toasts->UpdateAnimations(m_config, m_scanner.get());
            }
            EnsureAnimationTimer();
        } else if (wParam == 2) {
            // every 15 minutes automatically update watchlist
            StartGitHubSync();
        } else if (wParam == 3) {
            KillTimer(m_hWnd, 3);
            SetSaveStatusText(L"", 0);
        }
        return 0;
    }
    case WM_BW_GITHUB_UPDATED: {
        {
            std::lock_guard<std::mutex> lock(m_githubMutex);
            m_githubText = m_githubStatusMsg;
        }
        UpdateStatusDisplay();
        if (m_hWnd && IsWindow(m_hWnd)) {
            RECT rcClient;
            GetClientRect(m_hWnd, &rcClient);
            RECT rcSession = { 20, 64, rcClient.right - 20, 192 };
            InvalidateRect(m_hWnd, &rcSession, FALSE);
        }
        return 0;
    }
    case WM_BW_APP_UPDATE_RESULT: {
        if (wParam == 2) {
            // self-update installed on disk
            std::wstring exePath = Utils::GetExecutablePath();
            ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            DestroyWindow(hWnd);
            return 0;
        }

        std::wstring statusMsg;
        bool avail = false;
        std::wstring latestVer;
        {
            std::lock_guard<std::mutex> lock(m_githubMutex);
            statusMsg = m_updateStatusMsg;
            avail = m_updateAvailable;
            latestVer = m_latestVersion;
        }

        if (m_hAppUpdateBtn && IsWindow(m_hAppUpdateBtn)) {
            if (avail) {
                std::wstring btnLabel = L"Update " + latestVer;
                SetWindowTextW(m_hAppUpdateBtn, btnLabel.c_str());
            } else {
                SetWindowTextW(m_hAppUpdateBtn, (wParam == 1) ? L"Up to date \u2713" : L"Check updates");
            }
            InvalidateRect(m_hAppUpdateBtn, nullptr, FALSE);
        }
        if (wParam == 1 && !statusMsg.empty()) {
            SetSaveStatusText(statusMsg);
        }
        if (avail && m_toasts) {
            m_toasts->ShowToast(L"BorderWatch " + latestVer, L"Ally", L"New update ready! Click 'Update' in app", false, m_config);
            EnsureAnimationTimer();
        }
        return 0;
    }
    case WM_BW_TOGGLE_PANEL: {
        ToggleVisibility();
        return 0;
    }
    case WM_BW_OPEN_FROM_TOAST: {
        ShowPanel();
        return 0;
    }
    case WM_BW_TRAY_MSG: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ToggleVisibility();
        } else if (lParam == WM_RBUTTONUP) {
            m_tray->ShowContextMenu();
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case IDC_BTN_TOP_MIN:
            ShowWindow(m_hWnd, SW_MINIMIZE);
            break;
        case IDC_BTN_TOP_CLOSE:
            HidePanel();
            break;
        case IDC_BTN_DETECTED:
        case IDM_TRAY_DETECTED: {
            auto players = m_scanner->GetDetectedPlayers();
            int enemies = 0, allies = 0;
            for (const auto& p : players) {
                if (p.status == L"Enemy") enemies++;
                else allies++;
                if (p.avatar_png.empty()) {
                    m_scanner->RequestAvatarResolve(p.username, p.user_id);
                }
            }
            std::wstring summary = std::to_wstring(enemies) + L" enemies / " + std::to_wstring(allies) +
                                    L" allies detected in this session's log. Updated " + m_scanner->GetLastScanTime() + L".";
            m_detectedWnd->Show(players, summary, m_config, m_scanner->GetServerWebLink(), m_scanner->GetServerDeepLink());
            break;
        }
        case IDC_BTN_WEB_LINK:
        case IDM_TRAY_WEB_LINK: {
            CopyServerWebLink();
            break;
        }
        case IDC_BTN_DEEP_LINK:
        case IDM_TRAY_DEEP_LINK: {
            CopyServerDeepLink();
            break;
        }
        case IDC_BTN_APP_UPDATE: {
            if (m_updateAvailable && !m_updateDownloadUrl.empty()) {
                PerformSelfUpdate();
            } else {
                CheckForAppUpdates(true);
            }
            break;
        }
        case IDC_BTN_PAUSE:
        case IDM_TRAY_PAUSE: {
            bool paused = !m_scanner->IsPaused();
            m_scanner->SetPaused(paused);
            SetWindowTextW(m_hPauseBtn, paused ? L"Resume" : L"Pause");
            UpdateStatusDisplay();
            break;
        }
        case IDC_BTN_TEST_SOUND:
        case IDC_BTN_PREVIEW_ENEMY: {
            m_audio->Play(m_config.sound.enemy_file, m_config.sound.volume, m_dataDir);
            m_toasts->ShowToast(L"PreviewEnemy", L"Enemy", L"Border Patrol", false, m_config);
            EnsureAnimationTimer();
            SetSaveStatusText(L"\u25B6 Previewing Enemy alert");
            break;
        }
        case IDC_BTN_PREVIEW_ALLY: {
            m_audio->Play(m_config.sound.ally_file, m_config.sound.volume, m_dataDir);
            m_toasts->ShowToast(L"PreviewAlly", L"Ally", L"Allied Guard", false, m_config);
            EnsureAnimationTimer();
            SetSaveStatusText(L"\u25B6 Previewing Ally alert");
            break;
        }
        case IDC_BTN_AUTOSTART: {
            bool currentlyEnabled = StartupManager::IsAutostartEnabled();
            std::wstring err;
            if (StartupManager::SetAutostart(!currentlyEnabled, err)) {
                bool newState = StartupManager::IsAutostartEnabled();
                SetWindowTextW(m_hAutostartBtn, newState ? L"Remove from autostart" : L"Add to autostart");
                SetSaveStatusText(newState ? L"\u2713 Autostart enabled" : L"\u2713 Autostart removed");
            } else {
                SetSaveStatusText(L"Autostart error: " + err);
            }
            break;
        }
        case IDC_BTN_CHOOSE_SOUND:
            ChooseCustomSound(false);
            break;
        case IDC_BTN_CHOOSE_ALLY_SOUND:
            ChooseCustomSound(true);
            break;
        case IDC_CHK_ENEMIES:
        case IDC_CHK_ALLIES:
        case IDC_CHK_SOUND:
        case IDC_CHK_DARK_MODE:
        case IDC_CMB_SOUND:
        case IDC_CMB_ALLY_SOUND:
        case IDC_CMB_CORNER:
        case IDC_EDT_DURATION:
        case IDC_EDT_MAX_VISIBLE:
        case IDC_BTN_SAVE:
            SaveConfigFromControls();
            break;
        case IDC_SLD_VOLUME: {
            m_config.sound.volume = Slider_GetValue(m_hVolumeSlider);
            if (m_hWnd && IsWindow(m_hWnd)) {
                RECT rcVol = { 368, 378, 430, 410 };
                InvalidateRect(m_hWnd, &rcVol, FALSE);
            }
            if (HIWORD(wParam) == 1) {
                SaveConfigFromControls();
            }
            break;
        }
        case IDC_BTN_RESET:
            m_scanner->ResetSession();
            m_historyLines.clear();
            History_Clear(m_hHistoryBox);
            SetSaveStatusText(L"\u2713 Alert memory reset for new session");
            if (m_hWnd && IsWindow(m_hWnd)) {
                RECT rcClient;
                GetClientRect(m_hWnd, &rcClient);
                RECT rcCnt = { rcClient.right - 180, 546, rcClient.right - 16, 580 };
                InvalidateRect(m_hWnd, &rcCnt, FALSE);
            }
            break;
        case IDC_BTN_HIDE_TRAY:
            HidePanel();
            break;
        case IDM_TRAY_OPEN:
            ShowPanel();
            break;
        case IDC_BTN_QUIT:
        case IDM_TRAY_QUIT:
            DestroyWindow(m_hWnd);
            break;
        case 2001: {
            auto players = m_scanner->GetDetectedPlayers();
            int enemies = 0, allies = 0;
            for (const auto& p : players) {
                if (p.status == L"Enemy") enemies++;
                else allies++;
                if (p.avatar_png.empty()) {
                    m_scanner->RequestAvatarResolve(p.username, p.user_id);
                }
            }
            std::wstring summary = std::to_wstring(enemies) + L" enemies / " + std::to_wstring(allies) +
                                    L" allies detected in this session's log. Updated " + m_scanner->GetLastScanTime() + L".";
            m_detectedWnd->UpdateData(players, summary, m_config, m_scanner->GetServerWebLink(), m_scanner->GetServerDeepLink());
            break;
        }
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdcStatic = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        bool isDark = m_config.appearance.dark_mode;

        RECT rcCtrl;
        GetWindowRect(hCtrl, &rcCtrl);
        POINT pt = { rcCtrl.left, rcCtrl.top };
        ScreenToClient(hWnd, &pt);
        bool onCard = (pt.y >= 60 && pt.y <= 542);

        COLORREF bg = onCard ? (isDark ? RGB(30, 35, 48) : RGB(255, 255, 255))
                                : (isDark ? RGB(20, 24, 33) : RGB(244, 247, 252));
        COLORREF text = isDark ? RGB(243, 245, 252) : RGB(32, 37, 52);

        SetBkMode(hdcStatic, TRANSPARENT);
        SetBkColor(hdcStatic, bg);
        SetTextColor(hdcStatic, text);
        return (LRESULT)(onCard ? m_hCardBrush : m_hBgBrush);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdcEdit = (HDC)wParam;
        bool isDark = m_config.appearance.dark_mode;
        COLORREF bg = isDark ? RGB(34, 39, 52) : RGB(255, 255, 255);
        COLORREF text = isDark ? RGB(241, 243, 250) : RGB(36, 41, 56);
        SetBkColor(hdcEdit, bg);
        SetTextColor(hdcEdit, text);
        return (LRESULT)m_hCardBrush;
    }
    case WM_CLOSE:
        HidePanel();
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        KillTimer(hWnd, 2);
        if (m_scanner) m_scanner->Stop();
        m_tray->Remove();
        m_toasts->CloseAll();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
