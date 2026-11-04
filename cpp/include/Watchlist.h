#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <mutex>

struct PlayerRecord {
    std::wstring username;
    std::wstring display_name;
    uint64_t roblox_id = 0;
    std::wstring status; // 
    std::wstring clan;
};

class Watchlist {
public:
    Watchlist();

    bool LoadFromFile(const std::wstring& path);
    bool LoadFromJson(const std::string& json);
    bool UpdateFromGitHub(const std::wstring& localPath, std::wstring& outMessage, std::wstring& outError, const std::wstring& customUrl = L"");

    bool FindByUsername(const std::wstring& username, PlayerRecord& outRecord) const;
    bool FindById(uint64_t robloxId, PlayerRecord& outRecord) const;
    size_t Count() const;

private:
    static std::wstring ToLower(const std::wstring& str);
    static bool DownloadGitHubJson(const std::wstring& url, std::string& outJson, std::wstring& outError);

    mutable std::mutex m_mutex;
    std::vector<PlayerRecord> m_players;
    std::unordered_map<std::wstring, size_t> m_byLowerUsername;
    std::unordered_map<uint64_t, size_t> m_byId;
};
