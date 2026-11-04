#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

class StartupManager {
public:
    static bool IsAutostartEnabled();
    static bool SetAutostart(bool enabled, std::wstring& outError);

private:
    static std::wstring GetStartupShortcutPath();
};
