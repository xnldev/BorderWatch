#pragma once
#include <string>

#define BW_APP_VERSION L"5.0.2"

struct Config {
    bool enabled = true;
    bool anti_afk = true;
    bool notify_enemies = true;
    bool notify_allies = false;

    struct SoundConfig {
        bool enabled = true;
        bool enemies_only = false;
        double volume = 0.45;
        std::wstring file = L"meow-1.mp3";
        std::wstring enemy_file = L"meow-1.mp3";
        std::wstring ally_file = L"ping.wav";
    } sound;

    std::wstring watchlist_url = L"https://raw.githubusercontent.com/agentyautja/borderpatrol-list/main/players.json";
    std::wstring update_url = L"https://api.github.com/repos/xnldev/BorderWatch/releases/latest";

    struct NotificationConfig {
        std::wstring corner = L"bottom-right";
        double duration_seconds = 5.0;
        int width = 360;
        int margin = 24;
        double opacity = 0.97;
        std::wstring background = L"#F5F5F7";
        std::wstring text = L"#1D1D1F";
        std::wstring enemy_accent = L"#E5484D";
        std::wstring ally_accent = L"#30A46C";
        bool show_clan = true;
        int max_visible = 8;
    } notification;

    struct LocalConfig {
        int poll_interval_ms = 100;
        int burst_debounce_ms = 750;
        int initial_scan_seconds = 3;
    } local;

    struct AppearanceConfig {
        bool dark_mode = false;
    } appearance;

    static Config GetDefault();
    static Config LoadFromFile(const std::wstring& path);
    static Config LoadFromString(const std::string& json);
    bool SaveToFile(const std::wstring& path) const;
};
