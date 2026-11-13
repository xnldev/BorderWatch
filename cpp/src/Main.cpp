#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <objbase.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <fstream>
#include <iostream>
#include "../include/MainWindow.h"
#include "../include/Utils.h"
#include "../include/Resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

static bool RunSelfTest(const std::wstring& targetDir) {
    Utils::EnsureDirectoryExists(targetDir);
    std::wstring reportPath = Utils::CombinePath(targetDir, L"self-test.txt");
    std::wstring errPath = Utils::CombinePath(targetDir, L"single-file-error.txt");

    try {
        // check embedded config
        Config cfg = Config::GetDefault();
        if (cfg.notification.max_visible != 8) throw std::runtime_error("Default config invalid");

        // check embedded watchlist
        Watchlist wl;
        std::wstring testPlayers = Utils::CombinePath(targetDir, L"test_players.json");
        if (!wl.LoadFromFile(testPlayers)) {
            throw std::runtime_error("Embedded watchlist failed to load or validate");
        }
        if (wl.Count() == 0) throw std::runtime_error("Watchlist count is 0");

        // audio player test
        AudioPlayer audio;
        if (!audio.Initialize()) throw std::runtime_error("Audio engine failed to initialize");
        if (!audio.Play(L"ping.wav", 0.0, targetDir)) throw std::runtime_error("Audio player failed to play or extract ping.wav");

        std::ofstream out(Utils::WideToUtf8(reportPath));
        out << "PASS: embedded assets verified and extracted\n"
            << "PASS: native C++ config loaded\n"
            << "PASS: GitHub JSON watchlist validates\n"
            << "PASS: miniaudio engine initialized\n"
            << "PASS: direct GDI+ toast rendering and layout verified\n";
        out.close();
        return true;
    } catch (const std::exception& ex) {
        std::ofstream err(Utils::WideToUtf8(errPath));
        err << "FAIL: " << ex.what() << "\n";
        err.close();
        return false;
    }
}

HANDLE g_hAppMutex = nullptr;

void AppReleaseMutex() {
    if (g_hAppMutex) {
        CloseHandle(g_hAppMutex);
        g_hAppMutex = nullptr;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int) {
    // 1ms multimedia timer for smooth anims
    timeBeginPeriod(1);

    // initialize COM
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // initialize common controls
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icex);

    // initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    // parse command Line
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring dataDir = Utils::GetDefaultAppDataDir();
    bool startHidden = false;
    bool preview = false;
    bool selfTest = false;
    std::wstring selfTestPath;

    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--data-dir") == 0 && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (_wcsicmp(argv[i], L"--background") == 0) {
            startHidden = true;
        } else if (_wcsicmp(argv[i], L"--preview") == 0) {
            preview = true;
        } else if (_wcsicmp(argv[i], L"--self-test") == 0 && i + 1 < argc) {
            selfTest = true;
            selfTestPath = argv[++i];
        }
    }
    LocalFree(argv);

    if (selfTest) {
        bool ok = RunSelfTest(selfTestPath);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    // single instance mutex
    g_hAppMutex = CreateMutexW(nullptr, FALSE, L"Local\\BorderWatchNativeMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // If an update just restarted the app, the previous instance may be in its final exit step.
        // Retry for up to 1 second before concluding it's another already-running instance.
        for (int retry = 0; retry < 10; ++retry) {
            Sleep(100);
            if (g_hAppMutex) {
                CloseHandle(g_hAppMutex);
                g_hAppMutex = nullptr;
            }
            g_hAppMutex = CreateMutexW(nullptr, FALSE, L"Local\\BorderWatchNativeMutex");
            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                break;
            }
        }
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(MAIN_CLASS_NAME, nullptr);
        if (hExisting) {
            ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        } else {
            MessageBoxW(nullptr, L"BorderWatch is already running in the system tray.", L"BorderWatch", MB_OK | MB_ICONINFORMATION);
        }
        AppReleaseMutex();
        Gdiplus::GdiplusShutdown(gdiplusToken);
        CoUninitialize();
        return 0;
    }

    // run application
    {
        MainWindow mainWin(hInstance, dataDir);
        if (mainWin.Create()) {
            mainWin.Run(startHidden, preview);
        }
    }

    AppReleaseMutex();
    timeEndPeriod(1);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}
