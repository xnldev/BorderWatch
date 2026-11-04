#include "../include/LogScanner.h"
#include "../include/Utils.h"
#include "../third_party/yyjson.h"
#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <fstream>

#pragma comment(lib, "winhttp.lib")

LogScanner::LogScanner(const Watchlist& watchlist, const Config& config, const std::wstring& dataDir)
    : m_watchlist(watchlist), m_config(config), m_dataDir(dataDir) {}

LogScanner::~LogScanner() {
    Stop();
    if (m_hLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hLogFile);
        m_hLogFile = INVALID_HANDLE_VALUE;
    }
}

void LogScanner::Start() {
    if (m_workerThread.joinable()) return;
    m_stopRequested = false;
    m_workerThread = std::thread(&LogScanner::WorkerLoop, this);
    if (!m_resolverThread.joinable()) {
        m_resolverThread = std::thread(&LogScanner::ResolverLoop, this);
    }
}

void LogScanner::Stop() {
    m_stopRequested = true;
    m_resolverCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    if (m_resolverThread.joinable()) {
        m_resolverThread.join();
    }
}

void LogScanner::WorkerLoop() {
    while (!m_stopRequested) {
        int pollMs = 100;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            pollMs = std::clamp(m_config.local.poll_interval_ms, 50, 1000);
        }
        if (!m_paused) {
            try {
                Step();
            } catch (...) {}
        }
        for (int elapsed = 0; elapsed < pollMs && !m_stopRequested; elapsed += 25) {
            Sleep(25);
        }
    }
}

void LogScanner::UpdateConfig(const Config& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_config = config;
}

void LogScanner::ResetSessionUnlocked() {
    m_observed.clear();
    m_alertedNames.clear();
    m_alertedIds.clear();
    m_seenNames.clear();
    m_pendingEvents.clear();
    m_fileOffset = 0;
    m_partialLine.clear();
    m_currentServerId.clear();
    m_currentPlaceId.clear();
    m_ownUsername.clear();
    m_ownUserId = 0;
    m_phase = Phase::Initial;
    m_phaseUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    m_status = L"Scanning this session...";
    m_coverage = L"0 watchlist matches / 0 names checked / coverage incomplete";
}

void LogScanner::ResetSession() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ResetSessionUnlocked();
}

std::wstring LogScanner::GetCurrentTimestampStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

static bool QuickCheckLogHasGameJoin(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[65536];
    DWORD readBytes = 0;
    bool hasJoin = false;
    if (ReadFile(h, buf, sizeof(buf) - 1, &readBytes, nullptr) && readBytes > 0) {
        buf[readBytes] = '\0';
        if (strstr(buf, "Joining game") != nullptr || strstr(buf, "gameInstanceId") != nullptr) {
            hasJoin = true;
        }
    }
    CloseHandle(h);
    return hasJoin;
}

void LogScanner::FindRobloxProcessAndLog() {
    auto now = std::chrono::steady_clock::now();
    if (m_lastDiscoveryTime.time_since_epoch().count() > 0 &&
        std::chrono::duration_cast<std::chrono::seconds>(now - m_lastDiscoveryTime).count() < 2) {
        return;
    }
    m_lastDiscoveryTime = now;

    DWORD pid = 0;
    FILETIME processStartFt = { 0, 0 };
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe") == 0) {
                    pid = pe.th32ProcessID;
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (hProc) {
                        FILETIME ftCreate, ftExit, ftKernel, ftUser;
                        if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                            ULARGE_INTEGER uli;
                            uli.LowPart = ftCreate.dwLowDateTime;
                            uli.HighPart = ftCreate.dwHighDateTime;
                            // subtract 20s
                            if (uli.QuadPart > 200000000ULL) uli.QuadPart -= 200000000ULL;
                            processStartFt.dwLowDateTime = uli.LowPart;
                            processStartFt.dwHighDateTime = uli.HighPart;
                        }
                        CloseHandle(hProc);
                    }
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    m_robloxPid = pid;
    if (m_robloxPid == 0) {
        m_status = L"Local mode: waiting for Roblox";
        if (m_hLogFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hLogFile);
            m_hLogFile = INVALID_HANDLE_VALUE;
        }
        m_activeLogPath.clear();
        m_fileOffset = 0;
        m_partialLine.clear();
        return;
    }

    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        m_status = L"Cannot find LocalAppData folder";
        return;
    }
    std::wstring logDir = Utils::CombinePath(localAppData, L"Roblox\\logs");
    CoTaskMemFree(localAppData);

    std::wstring searchPattern = Utils::CombinePath(logDir, L"*Player*.log");
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);

    struct CandidateLog {
        std::wstring fullPath;
        FILETIME writeTime;
        bool hasJoin;
    };
    std::vector<CandidateLog> candidates;

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (wcsstr(fd.cFileName, L"CrashHandler") == nullptr) {
                    bool timeOk = (processStartFt.dwHighDateTime == 0 && processStartFt.dwLowDateTime == 0) ||
                                    (CompareFileTime(&fd.ftLastWriteTime, &processStartFt) >= 0) ||
                                    (CompareFileTime(&fd.ftCreationTime, &processStartFt) >= 0);
                    if (timeOk) {
                        std::wstring fullPath = Utils::CombinePath(logDir, fd.cFileName);
                        bool hasJoin = QuickCheckLogHasGameJoin(fullPath);
                        candidates.push_back({ fullPath, fd.ftLastWriteTime, hasJoin });
                    }
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::wstring newestFile;
    if (!candidates.empty()) {
        std::sort(candidates.begin(), candidates.end(), [](const CandidateLog& a, const CandidateLog& b) {
            if (a.hasJoin != b.hasJoin) {
                return a.hasJoin > b.hasJoin; // prefer real game experience log over launcher stub
            }
            return CompareFileTime(&a.writeTime, &b.writeTime) > 0;
        });
        newestFile = candidates[0].fullPath;
    } else {
        // check all non-crash player logs if time filter was too restrictive
        HANDLE hFallback = FindFirstFileW(searchPattern.c_str(), &fd);
        if (hFallback != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    if (wcsstr(fd.cFileName, L"CrashHandler") == nullptr) {
                        std::wstring fullPath = Utils::CombinePath(logDir, fd.cFileName);
                        bool hasJoin = QuickCheckLogHasGameJoin(fullPath);
                        candidates.push_back({ fullPath, fd.ftLastWriteTime, hasJoin });
                    }
                }
            } while (FindNextFileW(hFallback, &fd));
            FindClose(hFallback);
        }
        if (!candidates.empty()) {
            std::sort(candidates.begin(), candidates.end(), [](const CandidateLog& a, const CandidateLog& b) {
                if (a.hasJoin != b.hasJoin) return a.hasJoin > b.hasJoin;
                return CompareFileTime(&a.writeTime, &b.writeTime) > 0;
            });
            newestFile = candidates[0].fullPath;
        }
    }

    if (newestFile.empty()) {
        m_status = L"Local mode: waiting for the active player log";
        return;
    }

    if (m_activeLogPath != newestFile) {
        if (m_hLogFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hLogFile);
            m_hLogFile = INVALID_HANDLE_VALUE;
        }
        m_activeLogPath = newestFile;
        ResetSessionUnlocked();
    }
}

namespace {

bool IsHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool IsUsernameChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool ExtractGuid(const std::string& line, size_t startSearch, std::string& outGuid) {
    // 8-4-4-4-12 hex pattern (36 (32+4) chars)
    if (line.size() < startSearch + 36) return false;
    for (size_t i = startSearch; i + 36 <= line.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < 36; ++j) {
            char c = line[i + j];
            if (j == 8 || j == 13 || j == 18 || j == 23) {
                if (c != '-') { ok = false; break; }
            } else {
                if (!IsHexChar(c)) { ok = false; break; }
            }
        }
        if (ok) {
            outGuid = line.substr(i, 36);
            return true;
        }
    }
    return false;
}

bool ExtractPlaceId(const std::string& line, std::string& outPlaceId) {
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    const char* markers[] = { " place ", "placeid:", "placeid=", "placeid%3d", "\"placeid\":" };
    for (const char* m : markers) {
        size_t pos = lower.find(m);
        if (pos != std::string::npos) {
            size_t start = pos + strlen(m);
            while (start < line.size() && (line[start] == ' ' || line[start] == '"' || line[start] == '=' || line[start] == ':')) ++start;
            size_t end = start;
            while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) ++end;
            if (end > start) {
                outPlaceId = line.substr(start, end - start);
                return true;
            }
        }
    }
    return false;
}

bool ParseCreatorOutput(const std::string& line, std::string& outUsername) {
    static const std::string marker = "[FLog::CreatorOutput]";
    size_t pos = line.find(marker);
    if (pos == std::string::npos) return false;

    size_t i = pos + marker.size();
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return false;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;

    size_t nameStart = i;
    while (i < line.size() && IsUsernameChar(line[i])) ++i;
    size_t nameLen = i - nameStart;
    if (nameLen < 3 || nameLen > 20) return false;

    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i != line.size()) return false; // must be at end of line

    outUsername = line.substr(nameStart, nameLen);
    return true;
}

bool ParseChatRestriction(const std::string& line, std::string& outUsername, uint64_t& outUserId) {
    if (line.find("[FLog::CreatorWarning]") == std::string::npos) return false;
    static const std::string marker = "[ChatRestriction] CanChatWith";
    size_t pos = line.find(marker);
    if (pos == std::string::npos) return false;

    size_t i = pos + marker.size();
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return false;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;

    size_t nameStart = i;
    while (i < line.size() && IsUsernameChar(line[i])) ++i;
    size_t nameLen = i - nameStart;
    if (nameLen < 3 || nameLen > 20) return false;

    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i >= line.size() || line[i] != '(') return false;
    ++i; // skip '('

    size_t idStart = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    size_t idLen = i - idStart;
    if (idLen == 0 || idLen > 20 || i >= line.size() || line[i] != ')') return false;
    ++i; // skip ')'

    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i + 2 > line.size() || line[i] != '-' || line[i + 1] != '>') return false;
    i += 2;

    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (line.compare(i, 4, "true") == 0) {
        i += 4;
    } else if (line.compare(i, 5, "false") == 0) {
        i += 5;
    } else {
        return false;
    }

    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i != line.size()) return false;

    outUsername = line.substr(nameStart, nameLen);
    try {
        outUserId = std::stoull(line.substr(idStart, idLen));
    } catch (...) {
        return false;
    }
    return outUserId > 0;
}

bool ParseTeleportTicket(const std::string& line, std::string& outUsername, std::string& outDisplayName, uint64_t& outUserId) {
    static const std::string marker = "doTeleport: joinScriptUrl";
    if (line.find(marker) == std::string::npos) return false;

    try {
        // search for "UserName"%3a"
        size_t uPos = line.find("\"UserName\"%3a\"");
        if (uPos != std::string::npos && uPos + 14 < line.size()) {
            size_t start = uPos + 14;
            size_t end = line.find('"', start);
            if (end != std::string::npos && end >= start) {
                outUsername = line.substr(start, end - start);
            }
        }

        // search for "DisplayName"%3a"
        size_t dPos = line.find("\"DisplayName\"%3a\"");
        if (dPos != std::string::npos && dPos + 17 < line.size()) {
            size_t start = dPos + 17;
            size_t end = line.find('"', start);
            if (end != std::string::npos && end >= start) {
                outDisplayName = line.substr(start, end - start);
            }
        }

        // search for "UserId"%3a
        size_t idPos = line.find("\"UserId\"%3a");
        if (idPos != std::string::npos && idPos + 11 < line.size()) {
            size_t start = idPos + 11;
            size_t i = start;
            while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
            if (i > start) {
                try {
                    outUserId = std::stoull(line.substr(start, i - start));
                } catch (...) {}
            }
        }
    } catch (...) {
        return false;
    }

    return !outUsername.empty();
}

static bool FetchRobloxUserDetails(const std::wstring& username, uint64_t userId, uint64_t& outId, std::wstring& outDisplayName) {
    try {
        HINTERNET hSession = WinHttpOpen(L"BorderWatch/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        WinHttpSetTimeouts(hSession, 2500, 2500, 4000, 4000);

        HINTERNET hConnect = WinHttpConnect(hSession, L"users.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }

        HINTERNET hRequest = nullptr;
        std::string postBody;

        if (userId > 0) {
            std::wstring path = L"/v1/users/" + std::to_wstring(userId);
            hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        } else if (!username.empty()) {
            hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/v1/usernames/users", nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            postBody = "{\"usernames\":[\"" + Utils::WideToUtf8(username) + "\"],\"excludeBannedUsers\":false}";
        }

        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        BOOL sent = FALSE;
        if (userId > 0) {
            sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        } else {
            LPCWSTR headers = L"Content-Type: application/json\r\n";
            sent = WinHttpSendRequest(hRequest, headers, (DWORD)-1L, (LPVOID)postBody.data(), (DWORD)postBody.size(), (DWORD)postBody.size(), 0);
        }

        if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode != 200) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        std::string respBody;
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::vector<char> buf(bytesAvailable);
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
                respBody.append(buf.data(), bytesRead);
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (respBody.empty()) return false;

        yyjson_doc* doc = yyjson_read(respBody.c_str(), respBody.size(), 0);
        if (!doc) return false;

        yyjson_val* root = yyjson_doc_get_root(doc);
        if (!root) {
            yyjson_doc_free(doc);
            return false;
        }

        bool success = false;
        if (userId > 0) {
            yyjson_val* vDisp = yyjson_obj_get(root, "displayName");
            if (vDisp && yyjson_is_str(vDisp)) {
                outDisplayName = Utils::Utf8ToWide(yyjson_get_str(vDisp));
                outId = userId;
                success = true;
            }
        } else {
            yyjson_val* vData = yyjson_obj_get(root, "data");
            if (vData && yyjson_is_arr(vData)) {
                yyjson_val* first = yyjson_arr_get_first(vData);
                if (first && yyjson_is_obj(first)) {
                    yyjson_val* vId = yyjson_obj_get(first, "id");
                    yyjson_val* vDisp = yyjson_obj_get(first, "displayName");
                    if (vId && yyjson_is_uint(vId)) outId = yyjson_get_uint(vId);
                    else if (vId && yyjson_is_int(vId) && yyjson_get_int(vId) > 0) outId = (uint64_t)yyjson_get_int(vId);
                    if (vDisp && yyjson_is_str(vDisp)) outDisplayName = Utils::Utf8ToWide(yyjson_get_str(vDisp));
                    success = (outId > 0 || !outDisplayName.empty());
                }
            }
        }

        yyjson_doc_free(doc);
        return success;
    } catch (...) {
        return false;
    }
}

static bool DownloadBinaryUrl(const std::wstring& url, std::vector<uint8_t>& outBytes) {
    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[1024] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 1024;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) return false;

    HINTERNET hSession = WinHttpOpen(L"BorderWatch/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 2500, 2500, 4000, 4000);

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    outBytes.clear();
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
        size_t oldSize = outBytes.size();
        if (oldSize + avail > 2 * 1024 * 1024) break;
        outBytes.resize(oldSize + avail);
        DWORD readBytes = 0;
        if (WinHttpReadData(hRequest, outBytes.data() + oldSize, avail, &readBytes)) {
            outBytes.resize(oldSize + readBytes);
        } else {
            outBytes.resize(oldSize);
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return outBytes.size() > 8;
}

static void PruneAvatarDiskCache(const std::wstring& avatarDir, size_t maxFiles = 50) {
    if (avatarDir.empty() || !Utils::DirectoryExists(avatarDir)) return;
    std::wstring search = Utils::CombinePath(avatarDir, L"*.png");
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    struct FileEntry {
        std::wstring path;
        FILETIME lastWrite;
    };
    std::vector<FileEntry> files;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            FileEntry fe;
            fe.path = Utils::CombinePath(avatarDir, fd.cFileName);
            fe.lastWrite = fd.ftLastWriteTime;
            files.push_back(fe);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (files.size() > maxFiles) {
        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            return CompareFileTime(&a.lastWrite, &b.lastWrite) < 0; // oldest first
        });
        size_t toDelete = files.size() - maxFiles;
        for (size_t i = 0; i < toDelete; ++i) {
            DeleteFileW(files[i].path.c_str());
        }
    }
}

static bool FetchRobloxAvatarPng(uint64_t userId, const std::wstring& dataDir, std::vector<uint8_t>& outPng) {
    if (userId == 0) return false;
    try {
        std::wstring cacheFile;
        std::wstring avatarDir;
        if (!dataDir.empty()) {
            avatarDir = Utils::CombinePath(dataDir, L"avatars");
            Utils::EnsureDirectoryExists(avatarDir);
            cacheFile = Utils::CombinePath(avatarDir, std::to_wstring(userId) + L".png");
            if (Utils::FileExists(cacheFile)) {
                std::ifstream in(Utils::WideToUtf8(cacheFile), std::ios::binary | std::ios::ate);
                if (in.is_open()) {
                    size_t sz = (size_t)in.tellg();
                    if (sz > 8 && sz < 512 * 1024) {
                        outPng.resize(sz);
                        in.seekg(0, std::ios::beg);
                        in.read(reinterpret_cast<char*>(outPng.data()), sz);
                        if (outPng[0] == 0x89 && outPng[1] == 'P' && outPng[2] == 'N' && outPng[3] == 'G') {
                            in.close();
                            // touch last write time for LRU tracking
                            HANDLE hTouch = CreateFileW(cacheFile.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                            if (hTouch != INVALID_HANDLE_VALUE) {
                                FILETIME ftNow;
                                GetSystemTimeAsFileTime(&ftNow);
                                SetFileTime(hTouch, nullptr, nullptr, &ftNow);
                                CloseHandle(hTouch);
                            }
                            return true;
                        }
                    }
                }
            }
        }

        std::wstring thumbApiUrl = L"https://thumbnails.roblox.com/v1/users/avatar-headshot?userIds=" +
                                    std::to_wstring(userId) + L"&size=48x48&format=Png&isCircular=false";
        std::vector<uint8_t> jsonBytes;
        if (!DownloadBinaryUrl(thumbApiUrl, jsonBytes)) return false;

        yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(jsonBytes.data()), jsonBytes.size(), 0);
        if (!doc) return false;

        std::wstring imageUrl;
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (root) {
            yyjson_val* vData = yyjson_obj_get(root, "data");
            if (vData && yyjson_is_arr(vData)) {
                yyjson_val* first = yyjson_arr_get_first(vData);
                if (first && yyjson_is_obj(first)) {
                    yyjson_val* vUrl = yyjson_obj_get(first, "imageUrl");
                    if (vUrl && yyjson_is_str(vUrl)) {
                        imageUrl = Utils::Utf8ToWide(yyjson_get_str(vUrl));
                    }
                }
            }
        }
        yyjson_doc_free(doc);

        if (imageUrl.empty()) return false;
        if (!DownloadBinaryUrl(imageUrl, outPng)) return false;
        if (outPng.size() < 8 || outPng[0] != 0x89 || outPng[1] != 'P' || outPng[2] != 'N' || outPng[3] != 'G') {
            outPng.clear();
            return false;
        }

        if (!cacheFile.empty()) {
            std::ofstream out(Utils::WideToUtf8(cacheFile), std::ios::binary);
            if (out.is_open()) {
                out.write(reinterpret_cast<const char*>(outPng.data()), outPng.size());
                out.close();
                PruneAvatarDiskCache(avatarDir, 50);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

static bool ParseLogLineTimestamp(const std::string& line,
                                    std::wstring& outLocalTimeStr,
                                    std::chrono::system_clock::time_point& outUtcTp,
                                    double& outAgeSeconds) {
    // expected prefix: YYYY-MM-DDTHH:MM:SS.fffZ, or YYYY-MM-DDTHH:MM:SSZ,
    if (line.size() < 20 || line[4] != '-' || line[7] != '-' || line[10] != 'T' || line[13] != ':' || line[16] != ':') {
        return false;
    }
    auto parseDigits = [&](size_t pos, size_t len, int& outVal) -> bool {
        int val = 0;
        for (size_t i = 0; i < len; ++i) {
            char c = line[pos + i];
            if (c < '0' || c > '9') return false;
            val = val * 10 + (c - '0');
        }
        outVal = val;
        return true;
    };

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millis = 0;
    if (!parseDigits(0, 4, year) || !parseDigits(5, 2, month) || !parseDigits(8, 2, day) ||
        !parseDigits(11, 2, hour) || !parseDigits(14, 2, minute) || !parseDigits(17, 2, second)) {
        return false;
    }
    if (line.size() >= 23 && line[19] == '.') {
        parseDigits(20, 3, millis);
    }

    SYSTEMTIME stUtc = {};
    stUtc.wYear = (WORD)year;
    stUtc.wMonth = (WORD)month;
    stUtc.wDay = (WORD)day;
    stUtc.wHour = (WORD)hour;
    stUtc.wMinute = (WORD)minute;
    stUtc.wSecond = (WORD)second;
    stUtc.wMilliseconds = (WORD)millis;

    FILETIME ftUtc = {};
    if (!SystemTimeToFileTime(&stUtc, &ftUtc)) return false;

    SYSTEMTIME stLocal = {};
    if (!FileTimeToSystemTime(&ftUtc, &stUtc) || !SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal)) {
        stLocal = stUtc;
    }

    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d:%02d", stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
    outLocalTimeStr = buf;

    ULARGE_INTEGER uliLog;
    uliLog.LowPart = ftUtc.dwLowDateTime;
    uliLog.HighPart = ftUtc.dwHighDateTime;

    FILETIME ftNow = {};
    GetSystemTimeAsFileTime(&ftNow);
    ULARGE_INTEGER uliNow;
    uliNow.LowPart = ftNow.dwLowDateTime;
    uliNow.HighPart = ftNow.dwHighDateTime;

    // 100-ns intervals since 1601-01-01; Unix epoch is 116444736000000000ULL
    int64_t diff100ns = (int64_t)uliNow.QuadPart - (int64_t)uliLog.QuadPart;
    outAgeSeconds = (double)diff100ns / 10000000.0;

    const uint64_t UNIX_EPOCH_100NS = 116444736000000000ULL;
    if (uliLog.QuadPart > UNIX_EPOCH_100NS) {
        uint64_t msSinceEpoch = (uliLog.QuadPart - UNIX_EPOCH_100NS) / 10000ULL;
        outUtcTp = std::chrono::system_clock::time_point(std::chrono::milliseconds(msSinceEpoch));
    } else {
        outUtcTp = std::chrono::system_clock::now();
    }
    return true;
}

} // namespace

void LogScanner::ProcessLogData(const std::string& text, bool isHistorical) {
    auto nowSteady = std::chrono::steady_clock::now();
    std::wstring nowTimeStr = GetCurrentTimestampStr();

    size_t lineStart = 0;
    size_t textLen = text.size();

    while (lineStart < textLen) {
        size_t nlPos = text.find('\n', lineStart);
        size_t lineEnd = (nlPos == std::string::npos) ? textLen : nlPos;
        size_t len = lineEnd - lineStart;
        if (len > 0 && text[lineStart + len - 1] == '\r') --len;

        if (len > 0) {
            std::string line = text.substr(lineStart, len);

            // fast session check
            size_t joinPos = line.find("Joining game");
            if (joinPos == std::string::npos) joinPos = line.find("gameInstanceId");
            if (joinPos != std::string::npos) {
                std::string sid;
                if (ExtractGuid(line, joinPos, sid)) {
                    std::wstring wSid = Utils::Utf8ToWide(sid);
                    std::transform(wSid.begin(), wSid.end(), wSid.begin(), ::towlower);
                    bool newConn = (line.find("Joining game") != std::string::npos);

                    std::string pid;
                    if (ExtractPlaceId(line, pid)) {
                        m_currentPlaceId = Utils::Utf8ToWide(pid);
                    }

                    if (isHistorical || wSid != m_currentServerId || newConn) {
                        m_currentServerId = wSid;
                        m_observed.clear();
                        m_alertedNames.clear();
                        m_alertedIds.clear();
                        m_seenNames.clear();
                        if (!isHistorical) {
                            m_phase = Phase::Initial;
                            m_phaseUntil = nowSteady + std::chrono::milliseconds(300);
                            m_status = L"Scanning this session...";
                        }
                    }
                    lineStart = (nlPos == std::string::npos) ? textLen : nlPos + 1;
                    continue;
                }
            }

            if (m_currentPlaceId.empty()) {
                std::string pid;
                if (ExtractPlaceId(line, pid)) {
                    m_currentPlaceId = Utils::Utf8ToWide(pid);
                }
            }

            // fast teleport ticket check for local user details
            std::string tpUser, tpDisp;
            uint64_t tpId = 0;
            if (ParseTeleportTicket(line, tpUser, tpDisp, tpId)) {
                std::wstring wUser = Utils::Utf8ToWide(tpUser);
                std::wstring wDisp = Utils::Utf8ToWide(tpDisp);
                std::wstring lower = wUser;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                m_ownUsername = lower;
                if (tpId > 0) m_ownUserId = tpId;
                m_resolvedProfiles[lower] = { tpId, wDisp };
                m_observed.erase(lower);
            }

            std::wstring lineLocalTime = nowTimeStr;
            std::chrono::system_clock::time_point lineUtcTp = std::chrono::system_clock::now();
            double lineAgeSeconds = 0.0;
            bool hasTimestamp = ParseLogLineTimestamp(line, lineLocalTime, lineUtcTp, lineAgeSeconds);

            // ignore buffered old activity and future timestamps in live mode
            if (!isHistorical && hasTimestamp && (lineAgeSeconds > 30.0 || lineAgeSeconds < -5.0)) {
                lineStart = (nlPos == std::string::npos) ? textLen : nlPos + 1;
                continue;
            }

            // fast ChatRestriction check
            std::string nameStr;
            uint64_t robloxId = 0;
            if (ParseChatRestriction(line, nameStr, robloxId)) {
                std::wstring wName = Utils::Utf8ToWide(nameStr);
                std::wstring lower = wName;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                m_seenNames.insert(lower);

                PlayerRecord rec;
                bool found = m_watchlist.FindById(robloxId, rec);
                if (!found) found = m_watchlist.FindByUsername(wName, rec);

                if (found) {
                    std::wstring canonicalLower = rec.username;
                    std::transform(canonicalLower.begin(), canonicalLower.end(), canonicalLower.begin(), ::towlower);

                    // never alert or record the current local user's own activity
                    bool isSelf = (!m_ownUsername.empty() && (lower == m_ownUsername || canonicalLower == m_ownUsername)) ||
                                    (m_ownUserId > 0 && (robloxId == m_ownUserId || rec.roblox_id == m_ownUserId));
                    if (isSelf) {
                        lineStart = (nlPos == std::string::npos) ? textLen : nlPos + 1;
                        continue;
                    }

                    bool allowed = (rec.status == L"Enemy" && m_config.notify_enemies) ||
                                    (rec.status == L"Ally" && m_config.notify_allies);

                    DetectedPlayer dp;
                    dp.username = rec.username;
                    dp.display_name = rec.display_name;
                    dp.status = rec.status;
                    dp.clan = rec.clan;
                    dp.user_id = (robloxId > 0) ? robloxId : rec.roblox_id;
                    dp.last_seen_time = lineLocalTime;
                    dp.last_seen_utc = lineUtcTp;

                    auto resIt = m_resolvedProfiles.find(canonicalLower);
                    if (resIt != m_resolvedProfiles.end()) {
                        if (dp.user_id == 0 && resIt->second.first > 0) dp.user_id = resIt->second.first;
                        if (dp.display_name.empty() && !resIt->second.second.empty()) dp.display_name = resIt->second.second;
                    }
                    auto avIt = m_avatarCache.find(canonicalLower);
                    if (avIt != m_avatarCache.end()) {
                        dp.avatar_png = avIt->second;
                    }

                    m_observed[canonicalLower] = dp;

                    if (dp.display_name.empty() || dp.avatar_png.empty()) {
                        ResolvePlayerDetailsAsync(dp.username, dp.user_id);
                    }

                    if (!isHistorical && allowed && m_config.enabled && m_phase == Phase::Watching) {
                        bool isKnownPlayer = (m_alertedNames.find(canonicalLower) != m_alertedNames.end()) ||
                                             (m_alertedNames.find(lower) != m_alertedNames.end()) ||
                                             (dp.user_id > 0 && m_alertedIds.find(dp.user_id) != m_alertedIds.end());
                        if (!isKnownPlayer) {
                            m_alertedNames.insert(canonicalLower);
                            m_alertedNames.insert(lower);
                            if (dp.user_id > 0) m_alertedIds.insert(dp.user_id);

                            DetectionEvent ev;
                            ev.username = rec.username;
                            ev.status = rec.status;
                            ev.clan = rec.clan;
                            ev.user_id = dp.user_id;
                            ev.is_summary = false;
                            ev.server_id = m_currentServerId;
                            ev.avatar_png = dp.avatar_png;
                            m_pendingEvents.push_back(ev);
                            m_lastText = L"New detection: " + rec.username;
                        }
                    }
                }
                lineStart = (nlPos == std::string::npos) ? textLen : nlPos + 1;
                continue;
            }

            // fast CreatorOutput check
            if (ParseCreatorOutput(line, nameStr)) {
                std::wstring wName = Utils::Utf8ToWide(nameStr);
                std::wstring lower = wName;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                m_seenNames.insert(lower);

                PlayerRecord rec;
                if (m_watchlist.FindByUsername(wName, rec)) {
                    std::wstring canonicalLower = rec.username;
                    std::transform(canonicalLower.begin(), canonicalLower.end(), canonicalLower.begin(), ::towlower);

                    bool isSelf = (!m_ownUsername.empty() && (lower == m_ownUsername || canonicalLower == m_ownUsername)) ||
                                    (m_ownUserId > 0 && rec.roblox_id == m_ownUserId);
                    if (isSelf) {
                        lineStart = (nlPos == std::string::npos) ? textLen : nlPos + 1;
                        continue;
                    }

                    bool allowed = (rec.status == L"Enemy" && m_config.notify_enemies) ||
                                    (rec.status == L"Ally" && m_config.notify_allies);

                    DetectedPlayer dp;
                    dp.username = rec.username;
                    dp.display_name = rec.display_name;
                    dp.status = rec.status;
                    dp.clan = rec.clan;
                    dp.user_id = rec.roblox_id;
                    dp.last_seen_time = lineLocalTime;
                    dp.last_seen_utc = lineUtcTp;

                    auto resIt = m_resolvedProfiles.find(canonicalLower);
                    if (resIt != m_resolvedProfiles.end()) {
                        if (dp.user_id == 0 && resIt->second.first > 0) dp.user_id = resIt->second.first;
                        if (dp.display_name.empty() && !resIt->second.second.empty()) dp.display_name = resIt->second.second;
                    }
                    auto avIt = m_avatarCache.find(canonicalLower);
                    if (avIt != m_avatarCache.end()) {
                        dp.avatar_png = avIt->second;
                    }

                    m_observed[canonicalLower] = dp;

                    if (dp.display_name.empty() || dp.avatar_png.empty()) {
                        ResolvePlayerDetailsAsync(dp.username, dp.user_id);
                    }

                    if (!isHistorical && allowed && m_config.enabled && m_phase == Phase::Watching) {
                        bool isKnownPlayer = (m_alertedNames.find(canonicalLower) != m_alertedNames.end()) ||
                                             (m_alertedNames.find(lower) != m_alertedNames.end()) ||
                                             (dp.user_id > 0 && m_alertedIds.find(dp.user_id) != m_alertedIds.end());
                        if (!isKnownPlayer) {
                            m_alertedNames.insert(canonicalLower);
                            m_alertedNames.insert(lower);
                            if (dp.user_id > 0) m_alertedIds.insert(dp.user_id);

                            DetectionEvent ev;
                            ev.username = rec.username;
                            ev.status = rec.status;
                            ev.clan = rec.clan;
                            ev.user_id = dp.user_id;
                            ev.is_summary = false;
                            ev.server_id = m_currentServerId;
                            ev.avatar_png = dp.avatar_png;
                            m_pendingEvents.push_back(ev);
                            m_lastText = L"New detection: " + rec.username;
                        }
                    }
                }
            }
        }

        lineStart = (nlPos == std::string::npos) ? textLen : nlPos + 1;
    }
}

void LogScanner::CompleteInitialScan() {
    std::vector<DetectedPlayer> allowedList;
    for (const auto& pair : m_observed) {
        const auto& p = pair.second;
        bool allowed = (p.status == L"Enemy" && m_config.notify_enemies) ||
                        (p.status == L"Ally" && m_config.notify_allies);
        if (allowed) {
            allowedList.push_back(p);
            std::wstring lower = p.username;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            m_alertedNames.insert(lower);
            if (p.user_id > 0) {
                m_alertedIds.insert(p.user_id);
            }
        }
    }

    if (m_config.enabled && !allowedList.empty()) {
        // prioritize enemies before allies then sort by username
        std::sort(allowedList.begin(), allowedList.end(), [](const DetectedPlayer& a, const DetectedPlayer& b) {
            if (a.status != b.status) return a.status == L"Enemy";
            return a.username < b.username;
        });

        const size_t maxIndividualPings = 8;
        size_t count = std::min(allowedList.size(), maxIndividualPings);

        for (size_t i = 0; i < count; ++i) {
            auto& p = allowedList[i];
            std::wstring lower = p.username;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

            if (p.avatar_png.empty()) {
                auto avIt = m_avatarCache.find(lower);
                if (avIt != m_avatarCache.end()) p.avatar_png = avIt->second;
            }

            DetectionEvent ev;
            ev.username = p.username;
            ev.status = p.status;
            ev.clan = p.clan;
            ev.user_id = p.user_id;
            ev.is_summary = false;
            ev.server_id = m_currentServerId;
            ev.avatar_png = p.avatar_png;
            m_pendingEvents.push_back(ev);
        }

        if (allowedList.size() > maxIndividualPings) {
            size_t remaining = allowedList.size() - maxIndividualPings;
            DetectionEvent ev;
            ev.username = L"+" + std::to_wstring(remaining) + L" more on server";
            ev.status = L"Enemy";
            ev.clan = L"Initial session scan";
            ev.is_summary = true;
            ev.server_id = m_currentServerId;
            m_pendingEvents.push_back(ev);
        }
    }

    m_phase = Phase::Watching;
    m_status = L"Watching for new players";
    m_lastText = L"Initial scan finished: " + std::to_wstring(m_seenNames.size()) +
                 L" distinct names checked; " + std::to_wstring(m_observed.size()) +
                 L" watchlist matches. Later player activity is checked individually.";

    for (const auto& pair : m_observed) {
        if (pair.second.display_name.empty() || pair.second.avatar_png.empty()) {
            ResolvePlayerDetailsAsync(pair.second.username, pair.second.user_id);
        }
    }
}

void LogScanner::Step() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    FindRobloxProcessAndLog();
    if (m_robloxPid == 0 || m_activeLogPath.empty()) return;

    if (m_hLogFile == INVALID_HANDLE_VALUE) {
        m_hLogFile = CreateFileW(
            m_activeLogPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (m_hLogFile == INVALID_HANDLE_VALUE) {
            m_status = L"Cannot open Roblox log file";
            return;
        }
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(m_hLogFile, &fileSize)) return;

    if (fileSize.QuadPart < m_fileOffset) {
        ResetSessionUnlocked();
    }

    if (fileSize.QuadPart > m_fileOffset) {
        bool isInitialFullRead = (m_fileOffset == 0);
        LARGE_INTEGER pos;
        pos.QuadPart = m_fileOffset;
        SetFilePointerEx(m_hLogFile, pos, nullptr, FILE_BEGIN);

        int64_t remaining = fileSize.QuadPart - m_fileOffset;
        const DWORD chunkSize = 256 * 1024;
        std::vector<char> buffer(chunkSize);

        while (remaining > 0 && !m_stopRequested) {
            DWORD toRead = (DWORD)std::min<int64_t>(remaining, chunkSize);
            DWORD bytesRead = 0;
            if (!ReadFile(m_hLogFile, buffer.data(), toRead, &bytesRead, nullptr) || bytesRead == 0) {
                break;
            }
            m_fileOffset += bytesRead;
            remaining -= bytesRead;

            std::string chunk = m_partialLine + std::string(buffer.data(), bytesRead);
            size_t lastNl = chunk.find_last_of('\n');
            if (lastNl != std::string::npos) {
                std::string fullLines = chunk.substr(0, lastNl + 1);
                m_partialLine = chunk.substr(lastNl + 1);
                ProcessLogData(fullLines, isInitialFullRead);
            } else {
                m_partialLine = chunk;
            }
        }

        if (isInitialFullRead) {
            m_phase = Phase::Initial;
            m_phaseUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
            m_status = L"Scanning this session...";
            m_lastText = L"Initial scan: checking every available player name and ID in the current session log.";
        }
    }

    auto nowSteady = std::chrono::steady_clock::now();
    if (m_phase == Phase::Initial && nowSteady >= m_phaseUntil) {
        CompleteInitialScan();
    }

    m_coverage = std::to_wstring(m_observed.size()) + L" watchlist matches  /  " +
                 std::to_wstring(m_seenNames.size()) + L" names checked  /  coverage incomplete";
    m_lastScanTimeString = GetCurrentTimestampStr();
}

std::vector<DetectionEvent> LogScanner::DequeueEvents() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<DetectionEvent> events = std::move(m_pendingEvents);
    m_pendingEvents.clear();
    return events;
}

std::vector<DetectedPlayer> LogScanner::GetDetectedPlayers() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<DetectedPlayer> players;
    players.reserve(m_observed.size());
    for (const auto& pair : m_observed) {
        players.push_back(pair.second);
    }
    std::sort(players.begin(), players.end(), [](const DetectedPlayer& a, const DetectedPlayer& b) {
        if (a.status != b.status) return a.status == L"Enemy";
        return a.username < b.username;
    });
    return players;
}

std::wstring LogScanner::GetStatus() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_paused) return L"Paused";
    return m_status;
}

std::wstring LogScanner::GetCoverage() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_coverage;
}

std::wstring LogScanner::GetLastText() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_lastText;
}

std::wstring LogScanner::GetLastScanTime() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_lastScanTimeString;
}

std::wstring LogScanner::GetCurrentServerId() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_currentServerId;
}

std::wstring LogScanner::GetCurrentPlaceId() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_currentPlaceId;
}

std::wstring LogScanner::GetServerWebLink() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_currentServerId.empty()) return L"";
    if (!m_currentPlaceId.empty()) {
        return L"https://www.roblox.com/games/start?placeId=" + m_currentPlaceId + L"&gameInstanceId=" + m_currentServerId;
    }
    return L"https://www.roblox.com/games/start?gameInstanceId=" + m_currentServerId;
}

std::wstring LogScanner::GetServerDeepLink() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_currentServerId.empty()) return L"";
    if (!m_currentPlaceId.empty()) {
        return L"roblox://experiences/start?placeId=" + m_currentPlaceId + L"&gameInstanceId=" + m_currentServerId;
    }
    return L"roblox://experiences/start?gameInstanceId=" + m_currentServerId;
}

size_t LogScanner::GetTotalSeenNames() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_seenNames.size();
}

std::vector<uint8_t> LogScanner::GetAvatarForUser(const std::wstring& username) const {
    if (username.empty()) return {};
    std::wstring lower = username;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_avatarCache.find(lower);
    if (it != m_avatarCache.end()) {
        return it->second;
    }
    return {};
}

void LogScanner::RequestAvatarResolve(const std::wstring& username, uint64_t userId) {
    ResolvePlayerDetailsAsync(username, userId);
}

void LogScanner::ResolvePlayerDetailsAsync(const std::wstring& username, uint64_t userId) {
    if (username.empty() || m_stopRequested) return;
    std::wstring lower = username;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        bool hasProfile = (m_resolvedProfiles.find(lower) != m_resolvedProfiles.end());
        bool hasAvatar = (m_avatarCache.find(lower) != m_avatarCache.end());
        if (hasProfile && hasAvatar) {
            auto opIt = m_observed.find(lower);
            if (opIt != m_observed.end()) {
                const auto& prof = m_resolvedProfiles[lower];
                if (opIt->second.user_id == 0 && prof.first > 0) opIt->second.user_id = prof.first;
                if (opIt->second.display_name.empty() && !prof.second.empty()) opIt->second.display_name = prof.second;
                if (opIt->second.avatar_png.empty()) opIt->second.avatar_png = m_avatarCache[lower];
            }
            return;
        }
        if (m_inFlightResolves.find(lower) != m_inFlightResolves.end()) {
            return;
        }
        m_inFlightResolves.insert(lower);
    }

    {
        std::lock_guard<std::mutex> qLock(m_resolverMutex);
        m_resolveQueue.emplace_back(username, userId);
    }
    m_resolverCv.notify_one();
}

void LogScanner::ResolverLoop() {
    while (!m_stopRequested) {
        std::pair<std::wstring, uint64_t> task;
        {
            std::unique_lock<std::mutex> qLock(m_resolverMutex);
            m_resolverCv.wait(qLock, [this]() {
                return m_stopRequested.load() || !m_resolveQueue.empty();
            });
            if (m_stopRequested) break;
            task = std::move(m_resolveQueue.front());
            m_resolveQueue.pop_front();
        }

        std::wstring username = task.first;
        uint64_t userId = task.second;
        std::wstring lower = username;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

        uint64_t resolvedId = userId;
        std::wstring resolvedDisp;
        if (FetchRobloxUserDetails(username, userId, resolvedId, resolvedDisp) && !m_stopRequested) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_resolvedProfiles[lower] = { resolvedId, resolvedDisp };
            auto it = m_observed.find(lower);
            if (it != m_observed.end()) {
                if (resolvedId > 0) it->second.user_id = resolvedId;
                if (!resolvedDisp.empty()) it->second.display_name = resolvedDisp;
            }
        }

        if (resolvedId > 0 && !m_stopRequested) {
            std::vector<uint8_t> pngBytes;
            if (FetchRobloxAvatarPng(resolvedId, m_dataDir, pngBytes) && !m_stopRequested) {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                if (m_avatarCache.size() >= 50 && m_avatarCache.find(lower) == m_avatarCache.end()) {
                    m_avatarCache.erase(m_avatarCache.begin());
                }
                m_avatarCache[lower] = pngBytes;
                auto it = m_observed.find(lower);
                if (it != m_observed.end()) {
                    it->second.avatar_png = pngBytes;
                }
            }
        }

        if (!m_stopRequested) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_inFlightResolves.erase(lower);
        }
    }
}
