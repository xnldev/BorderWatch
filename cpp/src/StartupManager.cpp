#include "../include/StartupManager.h"
#include "../include/Utils.h"
#include <shlobj.h>
#include <shobjidl.h>

std::wstring StartupManager::GetStartupShortcutPath() {
    PWSTR startupFolder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &startupFolder))) {
        std::wstring path = Utils::CombinePath(startupFolder, L"BorderWatch.lnk");
        CoTaskMemFree(startupFolder);
        return path;
    }
    return L"";
}

bool StartupManager::IsAutostartEnabled() {
    std::wstring path = GetStartupShortcutPath();
    return !path.empty() && Utils::FileExists(path);
}

bool StartupManager::SetAutostart(bool enabled, std::wstring& outError) {
    std::wstring shortcutPath = GetStartupShortcutPath();
    if (shortcutPath.empty()) {
        outError = L"Windows Startup folder is unavailable.";
        return false;
    }

    if (!enabled) {
        if (Utils::FileExists(shortcutPath)) {
            if (!DeleteFileW(shortcutPath.c_str())) {
                outError = L"Failed to remove autostart shortcut.";
                return false;
            }
        }
        return true;
    }

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    wchar_t exeDir[MAX_PATH] = { 0 };
    wcscpy_s(exeDir, exePath);
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (FAILED(hr)) {
        outError = L"Failed to create COM ShellLink instance.";
        return false;
    }

    psl->SetPath(exePath);
    psl->SetArguments(L"--background");
    psl->SetWorkingDirectory(exeDir);
    psl->SetDescription(L"Start BorderWatch in the background at sign-in");

    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
    if (SUCCEEDED(hr)) {
        hr = ppf->Save(shortcutPath.c_str(), TRUE);
        ppf->Release();
    } else {
        outError = L"Failed to query IPersistFile interface.";
    }

    psl->Release();
    return SUCCEEDED(hr);
}
