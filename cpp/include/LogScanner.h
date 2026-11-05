#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <deque>
#include "Watchlist.h"
#include "Config.h"

struct DetectedPlayer {
    std::wstring username;
    std::wstring display_name;
    std::wstring status;
    std::wstring clan;
    uint64_t user_id = 0;
    std::wstring last_seen_time;
    std::chrono::system_clock::time_point last_seen_utc;
    std::vector<uint8_t> avatar_png;
};

struct DetectionEvent {
    std::wstring username;
    std::wstring status;
    std::wstring clan;
    uint64_t user_id = 0;
    bool is_summary = false;
    std::wstring server_id;
    std::vector<uint8_t> avatar_png;
};

class LogScanner {
public:
    LogScanner(const Watchlist& watchlist, const Config& config, const std::wstring& dataDir = L"");
    ~LogScanner();

    void Start();
    void Stop();

    void UpdateConfig(const Config& config);
    void Step();
    void ResetSession();
    void ClearDetectedPlayers();
    void RemoveDetectedPlayer(const std::wstring& username);
    void PruneInactivePlayersUnlocked(std::chrono::seconds timeoutSec = std::chrono::seconds(600));
    void SetPaused(bool paused) { m_paused = paused; }
    bool IsPaused() const { return m_paused; }

    std::vector<DetectionEvent> DequeueEvents();
    std::vector<DetectedPlayer> GetDetectedPlayers() const;
    std::vector<uint8_t> GetAvatarForUser(const std::wstring& username) const;
    void RequestAvatarResolve(const std::wstring& username, uint64_t userId);

    std::wstring GetStatus() const;
    std::wstring GetCoverage() const;
    std::wstring GetLastText() const;
    std::wstring GetLastScanTime() const;
    std::wstring GetCurrentServerId() const;
    std::wstring GetCurrentPlaceId() const;
    std::wstring GetServerWebLink() const;
    std::wstring GetServerDeepLink() const;
    size_t GetTotalSeenNames() const;

private:
    enum class Phase {
        Initial,
        Watching
    };

    void WorkerLoop();
    void ResolverLoop();
    void ResetSessionUnlocked();
    void FindRobloxProcessAndLog();
    void ProcessLogData(const std::string& text, bool isHistorical);
    void CompleteInitialScan();
    void ResolvePlayerDetailsAsync(const std::wstring& username, uint64_t userId);
    static std::wstring GetCurrentTimestampStr();

    const Watchlist& m_watchlist;
    Config m_config;
    std::wstring m_dataDir;
    mutable std::recursive_mutex m_mutex;

    std::thread m_workerThread;
    std::thread m_resolverThread;
    std::mutex m_resolverMutex;
    std::condition_variable m_resolverCv;
    std::deque<std::pair<std::wstring, uint64_t>> m_resolveQueue;
    std::atomic<bool> m_stopRequested{ false };
    std::atomic<bool> m_paused{ false };

    DWORD m_robloxPid = 0;
    std::wstring m_activeLogPath;
    HANDLE m_hLogFile = INVALID_HANDLE_VALUE;
    int64_t m_fileOffset = 0;
    std::string m_partialLine;

    std::wstring m_ownUsername;
    uint64_t m_ownUserId = 0;

    std::wstring m_currentServerId;
    std::wstring m_currentPlaceId;
    Phase m_phase = Phase::Watching;
    std::chrono::steady_clock::time_point m_phaseUntil;
    std::chrono::steady_clock::time_point m_lastDiscoveryTime;

    std::unordered_map<std::wstring, DetectedPlayer> m_observed; // lower username
    std::unordered_map<std::wstring, std::pair<uint64_t, std::wstring>> m_resolvedProfiles; // lower username -> {id, displayName}
    std::unordered_map<std::wstring, std::vector<uint8_t>> m_avatarCache; // lower username -> png bytes
    std::unordered_set<std::wstring> m_inFlightResolves;
    std::unordered_set<std::wstring> m_alertedNames;
    std::unordered_set<uint64_t> m_alertedIds;
    std::unordered_set<std::wstring> m_seenNames;

    std::vector<DetectionEvent> m_pendingEvents;

    std::wstring m_status = L"Getting ready...";
    std::wstring m_coverage;
    std::wstring m_lastText;
    std::wstring m_lastScanTimeString = L"--:--:--";
};
