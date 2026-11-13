#include "../include/AntiAfk.h"
#include "../include/LogScanner.h"
#include <random>

AntiAfk::AntiAfk(const LogScanner* scanner)
    : m_scanner(scanner) {
}

AntiAfk::~AntiAfk() {
    Stop();
}

void AntiAfk::Start() {
    if (m_workerThread.joinable()) return;
    m_stopRequested = false;
    m_workerThread = std::thread(&AntiAfk::WorkerLoop, this);
}

void AntiAfk::Stop() {
    m_stopRequested = true;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void AntiAfk::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

bool AntiAfk::IsEnabled() const {
    return m_enabled.load();
}

HWND AntiAfk::FindRobloxWindow(DWORD targetPid) {
    HWND hWnd = FindWindowW(L"WINDOWSCLIENT", nullptr);
    if (hWnd && IsWindow(hWnd)) {
        if (targetPid != 0) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hWnd, &pid);
            if (pid == targetPid) return hWnd;
        } else {
            return hWnd;
        }
    }

    struct EnumData {
        DWORD targetPid;
        HWND result;
    } data = { targetPid, nullptr };

    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* d = reinterpret_cast<EnumData*>(lp);
        if (!IsWindow(h)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (d->targetPid != 0 && pid != d->targetPid) return TRUE;

        wchar_t className[64] = {};
        GetClassNameW(h, className, 64);
        if (_wcsicmp(className, L"WINDOWSCLIENT") == 0) {
            d->result = h;
            return FALSE;
        }
        wchar_t title[128] = {};
        GetWindowTextW(h, title, 128);
        if (wcsstr(title, L"Roblox") != nullptr) {
            d->result = h;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}

void AntiAfk::PulseRoblox(HWND hRoblox, bool isForeground) {
    if (!hRoblox || !IsWindow(hRoblox)) return;

    RECT rc = {};
    GetClientRect(hRoblox, &rc);
    int cx = (rc.right > 40) ? (rc.right / 2) : 100;
    int cy = (rc.bottom > 40) ? (rc.bottom / 2) : 100;
    LPARAM lParam = MAKELPARAM(cx, cy);

    PostMessageW(hRoblox, WM_MOUSEMOVE, 0, lParam);
    PostMessageW(hRoblox, WM_RBUTTONDOWN, MK_RBUTTON, lParam);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    PostMessageW(hRoblox, WM_RBUTTONUP, 0, lParam);

    UINT sc = MapVirtualKeyW(VK_F15, MAPVK_VK_TO_VSC);
    PostMessageW(hRoblox, WM_KEYDOWN, VK_F15, 1 | (sc << 16));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    PostMessageW(hRoblox, WM_KEYUP, VK_F15, 1 | (sc << 16) | (1 << 30) | (1 << 31));

    if (isForeground && GetForegroundWindow() == hRoblox) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_F15;
        inputs[0].ki.wScan = static_cast<WORD>(sc);

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_F15;
        inputs[1].ki.wScan = static_cast<WORD>(sc);
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));

        mouse_event(MOUSEEVENTF_MOVE, 0, 0, 0, 0);
    }
}

void AntiAfk::WorkerLoop() {
    std::mt19937 rng(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> distIntervalSec(180, 270);

    auto nextPulseTime = std::chrono::steady_clock::now() + std::chrono::seconds(distIntervalSec(rng));

    while (!m_stopRequested.load()) {
        for (int i = 0; i < 5; ++i) {
            if (m_stopRequested.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        if (!m_enabled.load()) continue;

        DWORD targetPid = 0;
        if (m_scanner) {
            targetPid = m_scanner->GetRobloxPid();
        }

        HWND hRoblox = FindRobloxWindow(targetPid);
        if (!hRoblox || !IsWindow(hRoblox)) {
            continue;
        }

        HWND hForeground = GetForegroundWindow();
        bool isRobloxForeground = (hForeground == hRoblox);

        bool shouldPulse = false;

        if (!isRobloxForeground) {
            auto now = std::chrono::steady_clock::now();
            if (now >= nextPulseTime) {
                shouldPulse = true;
            }
        } else {
            LASTINPUTINFO lii = { sizeof(LASTINPUTINFO) };
            if (GetLastInputInfo(&lii)) {
                DWORD nowTick = GetTickCount();
                DWORD idleMs = nowTick - lii.dwTime;

                if (idleMs >= 150000) {
                    auto now = std::chrono::steady_clock::now();
                    if (now >= nextPulseTime) {
                        shouldPulse = true;
                    }
                } else {
                    nextPulseTime = std::chrono::steady_clock::now() + std::chrono::seconds(distIntervalSec(rng));
                }
            }
        }

        if (shouldPulse) {
            PulseRoblox(hRoblox, isRobloxForeground);
            nextPulseTime = std::chrono::steady_clock::now() + std::chrono::seconds(distIntervalSec(rng));
        }
    }
}
