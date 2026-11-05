#include "../include/Watchlist.h"
#include "../include/Utils.h"
#include "../include/Resource.h"
#include "../third_party/yyjson.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")

Watchlist::Watchlist() {}

std::wstring Watchlist::ToLower(const std::wstring& str) {
    std::wstring out = str;
    std::transform(out.begin(), out.end(), out.begin(), ::towlower);
    return out;
}

bool Watchlist::LoadFromJson(const std::string& json) {
    if (json.empty()) return false;

    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (!doc) return false;

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_val* vVersion = yyjson_obj_get(root, "version");
    yyjson_val* vCount = yyjson_obj_get(root, "count");
    yyjson_val* vPlayers = yyjson_obj_get(root, "players");

    if (!vVersion || !yyjson_is_int(vVersion) || yyjson_get_int(vVersion) != 1) {
        yyjson_doc_free(doc);
        return false;
    }
    if (!vPlayers || !yyjson_is_arr(vPlayers)) {
        yyjson_doc_free(doc);
        return false;
    }

    size_t count = yyjson_arr_size(vPlayers);
    if (count == 0 || count > 100000) {
        yyjson_doc_free(doc);
        return false;
    }
    if (!vCount || !yyjson_is_int(vCount) || (size_t)yyjson_get_int(vCount) != count) {
        yyjson_doc_free(doc);
        return false;
    }

    auto isValidRobloxUsername = [](const std::string& s) -> bool {
        if (s.size() < 3 || s.size() > 20) return false;
        for (unsigned char c : s) {
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok) return false;
        }
        return true;
    };

    std::vector<PlayerRecord> newPlayers;
    std::unordered_map<std::wstring, size_t> newByLower;
    std::unordered_map<uint64_t, size_t> newById;
    newPlayers.reserve(count);

    size_t idx, max;
    yyjson_val* item;
    yyjson_arr_foreach(vPlayers, idx, max, item) {
        if (!yyjson_is_obj(item)) {
            yyjson_doc_free(doc);
            return false;
        }

        yyjson_val* vName = yyjson_obj_get(item, "username");
        yyjson_val* vDisp = yyjson_obj_get(item, "display_name");
        yyjson_val* vId = yyjson_obj_get(item, "roblox_id");
        yyjson_val* vStatus = yyjson_obj_get(item, "status");
        yyjson_val* vClan = yyjson_obj_get(item, "clan");

        if (!vName || !yyjson_is_str(vName) || !vId || !vStatus || !yyjson_is_str(vStatus) || !vClan) {
            yyjson_doc_free(doc);
            return false;
        }

        std::string nameStr = yyjson_get_str(vName);
        if (!isValidRobloxUsername(nameStr)) {
            yyjson_doc_free(doc);
            return false;
        }

        std::string dispStr = (vDisp && yyjson_is_str(vDisp)) ? yyjson_get_str(vDisp) : "";
        std::string statusStr = yyjson_get_str(vStatus);
        std::string clanStr = (vClan && yyjson_is_str(vClan)) ? yyjson_get_str(vClan) : "";

        if (statusStr != "Enemy" && statusStr != "Ally") {
            yyjson_doc_free(doc);
            return false;
        }

        uint64_t rId = 0;
        if (yyjson_is_uint(vId)) rId = yyjson_get_uint(vId);
        else if (yyjson_is_int(vId) && yyjson_get_int(vId) > 0) rId = (uint64_t)yyjson_get_int(vId);
        else if (yyjson_is_str(vId)) {
            try { rId = std::stoull(yyjson_get_str(vId)); } catch (...) { rId = 0; }
        }
        if (rId == 0) {
            yyjson_doc_free(doc);
            return false;
        }

        PlayerRecord rec;
        rec.username = Utils::Utf8ToWide(nameStr);
        rec.display_name = Utils::Utf8ToWide(dispStr);
        rec.roblox_id = rId;
        rec.status = Utils::Utf8ToWide(statusStr);
        rec.clan = Utils::Utf8ToWide(clanStr);

        std::wstring lower = ToLower(rec.username);
        if (newByLower.find(lower) != newByLower.end() || newById.find(rId) != newById.end()) {
            yyjson_doc_free(doc);
            return false;
        }

        size_t playerIdx = newPlayers.size();
        newPlayers.push_back(rec);
        newByLower[lower] = playerIdx;
        newById[rId] = playerIdx;
    }

    yyjson_doc_free(doc);

    if (newPlayers.empty()) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_players = std::move(newPlayers);
    m_byLowerUsername = std::move(newByLower);
    m_byId = std::move(newById);
    return true;
}

bool Watchlist::LoadFromFile(const std::wstring& path) {
    if (Utils::FileExists(path)) {
        std::string pathUtf8 = Utils::WideToUtf8(path);
        std::ifstream file(pathUtf8, std::ios::binary);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            if (LoadFromJson(ss.str())) return true;
        }
    }

    // Fallback to embedded resource
    std::vector<uint8_t> res = Utils::LoadResourceBytes(IDR_JSON_PLAYERS);
    if (!res.empty()) {
        std::string json(reinterpret_cast<const char*>(res.data()), res.size());
        if (LoadFromJson(json)) {
            std::string pathUtf8 = Utils::WideToUtf8(path);
            std::ofstream out(pathUtf8, std::ios::binary);
            if (out.is_open()) out.write(reinterpret_cast<const char*>(res.data()), res.size());
            return true;
        }
    }

    return false;
}

bool Watchlist::DownloadGitHubJson(const std::wstring& customUrl, std::string& outJson, std::wstring& outError) {
    std::wstring targetUrl = customUrl.empty()
        ? L"https://raw.githubusercontent.com/agentyautja/borderpatrol-list/main/players.json"
        : customUrl;

    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[1024] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 1024;

    if (!WinHttpCrackUrl(targetUrl.c_str(), (DWORD)targetUrl.length(), 0, &urlComp)) {
        outError = L"Invalid watchlist URL format.";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"BorderWatch/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = L"WinHttpOpen failed: " + std::to_wstring(GetLastError());
        return false;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        outError = L"WinHttpConnect failed: " + std::to_wstring(GetLastError());
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
                                           urlPath,
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           flags);
    if (!hRequest) {
        outError = L"WinHttpOpenRequest failed: " + std::to_wstring(GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    LPCWSTR headers = L"Cache-Control: no-cache\r\nPragma: no-cache\r\n";
    BOOL sent = WinHttpSendRequest(hRequest, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        outError = L"WinHttpSendRequest/Receive failed: " + std::to_wstring(GetLastError());
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
        outError = L"GitHub returned HTTP status " + std::to_wstring(statusCode);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::string response;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            response.append(buffer.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (response.empty() || response.size() > 20 * 1024 * 1024) {
        outError = L"Invalid response size from GitHub";
        return false;
    }

    outJson = std::move(response);
    return true;
}

bool Watchlist::UpdateFromGitHub(const std::wstring& localPath, std::wstring& outMessage, std::wstring& outError, const std::wstring& customUrl) {
    std::string json;
    std::wstring err;
    if (DownloadGitHubJson(customUrl, json, err)) {
        if (LoadFromJson(json)) {
            std::string pathUtf8 = Utils::WideToUtf8(localPath);
            std::string tmpPathUtf8 = pathUtf8 + ".tmp";
            std::ofstream out(tmpPathUtf8, std::ios::binary);
            if (out.is_open()) {
                out.write(json.data(), json.size());
                out.close();
                std::wstring tmpPath = localPath + L".tmp";
                MoveFileExW(tmpPath.c_str(), localPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t timeBuf[32];
            swprintf_s(timeBuf, L"%02d:%02d", st.wHour, st.wMinute);
            outMessage = L"GitHub list updated at " + std::wstring(timeBuf);
            outError = L"";
            return true;
        } else {
            err = L"Downloaded GitHub watchlist JSON failed validation.";
        }
    }

    outError = err;
    bool fallbackOk = LoadFromFile(localPath);
    if (fallbackOk) {
        outMessage = L"GitHub refresh failed \u2014 using saved list";
    } else {
        outMessage = L"GitHub refresh failed and no list is available";
    }
    return fallbackOk;
}

bool Watchlist::FindByUsername(const std::wstring& username, PlayerRecord& outRecord) const {
    std::wstring lower = ToLower(username);
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_byLowerUsername.find(lower);
    if (it != m_byLowerUsername.end()) {
        outRecord = m_players[it->second];
        return true;
    }
    return false;
}

bool Watchlist::FindById(uint64_t robloxId, PlayerRecord& outRecord) const {
    if (robloxId == 0) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_byId.find(robloxId);
    if (it != m_byId.end()) {
        outRecord = m_players[it->second];
        return true;
    }
    return false;
}

size_t Watchlist::Count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_players.size();
}

std::vector<PlayerRecord> Watchlist::GetAllPlayers() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_players;
}
