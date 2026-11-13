#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <thread>
#include <chrono>

class LogScanner;

class AntiAfk {
public:
    explicit AntiAfk(const LogScanner* scanner = nullptr);
    ~AntiAfk();

    void Start();
    void Stop();
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

private:
    void WorkerLoop();
    HWND FindRobloxWindow(DWORD targetPid);
    void PulseRoblox(HWND hRoblox, bool isForeground);

    const LogScanner* m_scanner = nullptr;
    std::atomic<bool> m_enabled{ true };
    std::atomic<bool> m_stopRequested{ false };
    std::thread m_workerThread;
};
