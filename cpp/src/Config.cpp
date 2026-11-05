#include "../include/Config.h"
#include "../include/Utils.h"
#include "../include/Resource.h"
#include "../third_party/yyjson.h"
#include <fstream>
#include <algorithm>

Config Config::GetDefault() {
    Config cfg;
    std::vector<uint8_t> res = Utils::LoadResourceBytes(IDR_JSON_SETTINGS);
    if (!res.empty()) {
        std::string json(reinterpret_cast<const char*>(res.data()), res.size());
        return LoadFromString(json);
    }
    return cfg;
}

Config Config::LoadFromString(const std::string& json) {
    Config cfg;
    if (json.empty()) return cfg;

    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (!doc) return cfg;

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return cfg;
    }

    yyjson_val* v = nullptr;
    if ((v = yyjson_obj_get(root, "enabled")) && yyjson_is_bool(v)) {
        cfg.enabled = yyjson_get_bool(v);
    }
    if ((v = yyjson_obj_get(root, "notify_enemies")) && yyjson_is_bool(v)) {
        cfg.notify_enemies = yyjson_get_bool(v);
    }
    if ((v = yyjson_obj_get(root, "notify_allies")) && yyjson_is_bool(v)) {
        cfg.notify_allies = yyjson_get_bool(v);
    }

    if ((v = yyjson_obj_get(root, "watchlist_url")) && yyjson_is_str(v)) {
        std::wstring url = Utils::Utf8ToWide(yyjson_get_str(v));
        if (!url.empty()) cfg.watchlist_url = url;
    }
    if ((v = yyjson_obj_get(root, "update_url")) && yyjson_is_str(v)) {
        std::wstring url = Utils::Utf8ToWide(yyjson_get_str(v));
        if (url.find(L"agentyautja/borderpatrol-list") != std::wstring::npos) {
            url = L"https://api.github.com/repos/xnldev/BorderWatch/releases/latest";
        }
        if (!url.empty()) cfg.update_url = url;
    }

    // sound
    yyjson_val* sound = yyjson_obj_get(root, "sound");
    if (sound && yyjson_is_obj(sound)) {
        if ((v = yyjson_obj_get(sound, "enabled")) && yyjson_is_bool(v)) cfg.sound.enabled = yyjson_get_bool(v);
        if ((v = yyjson_obj_get(sound, "enemies_only")) && yyjson_is_bool(v)) cfg.sound.enemies_only = yyjson_get_bool(v);
        if ((v = yyjson_obj_get(sound, "volume")) && yyjson_is_num(v)) cfg.sound.volume = std::clamp(yyjson_get_num(v), 0.0, 1.0);
        if ((v = yyjson_obj_get(sound, "file")) && yyjson_is_str(v)) {
            cfg.sound.file = Utils::Utf8ToWide(yyjson_get_str(v));
            cfg.sound.enemy_file = cfg.sound.file;
        }
        if ((v = yyjson_obj_get(sound, "enemy_file")) && yyjson_is_str(v)) {
            std::wstring ef = Utils::Utf8ToWide(yyjson_get_str(v));
            if (!ef.empty()) {
                cfg.sound.enemy_file = ef;
                cfg.sound.file = ef;
            }
        }
        if ((v = yyjson_obj_get(sound, "ally_file")) && yyjson_is_str(v)) {
            std::wstring af = Utils::Utf8ToWide(yyjson_get_str(v));
            if (!af.empty()) cfg.sound.ally_file = af;
        }
    }

    // notification
    yyjson_val* notif = yyjson_obj_get(root, "notification");
    if (notif && yyjson_is_obj(notif)) {
        if ((v = yyjson_obj_get(notif, "corner")) && yyjson_is_str(v)) cfg.notification.corner = Utils::Utf8ToWide(yyjson_get_str(v));
        if ((v = yyjson_obj_get(notif, "duration_seconds")) && yyjson_is_num(v)) cfg.notification.duration_seconds = std::clamp(yyjson_get_num(v), 1.0, 30.0);
        if ((v = yyjson_obj_get(notif, "width")) && yyjson_is_int(v)) cfg.notification.width = std::clamp(yyjson_get_int(v), 280, 600);
        if ((v = yyjson_obj_get(notif, "margin")) && yyjson_is_int(v)) cfg.notification.margin = std::clamp(yyjson_get_int(v), 0, 200);
        if ((v = yyjson_obj_get(notif, "opacity")) && yyjson_is_num(v)) cfg.notification.opacity = std::clamp(yyjson_get_num(v), 0.3, 1.0);
        if ((v = yyjson_obj_get(notif, "background")) && yyjson_is_str(v)) cfg.notification.background = Utils::Utf8ToWide(yyjson_get_str(v));
        if ((v = yyjson_obj_get(notif, "text")) && yyjson_is_str(v)) cfg.notification.text = Utils::Utf8ToWide(yyjson_get_str(v));
        if ((v = yyjson_obj_get(notif, "enemy_accent")) && yyjson_is_str(v)) cfg.notification.enemy_accent = Utils::Utf8ToWide(yyjson_get_str(v));
        if ((v = yyjson_obj_get(notif, "ally_accent")) && yyjson_is_str(v)) cfg.notification.ally_accent = Utils::Utf8ToWide(yyjson_get_str(v));
        if ((v = yyjson_obj_get(notif, "show_clan")) && yyjson_is_bool(v)) cfg.notification.show_clan = yyjson_get_bool(v);
        if ((v = yyjson_obj_get(notif, "max_visible")) && yyjson_is_int(v)) cfg.notification.max_visible = std::clamp(yyjson_get_int(v), 1, 20);
    }

    // local
    yyjson_val* local = yyjson_obj_get(root, "local");
    if (local && yyjson_is_obj(local)) {
        if ((v = yyjson_obj_get(local, "poll_interval_ms")) && yyjson_is_int(v)) cfg.local.poll_interval_ms = std::clamp(yyjson_get_int(v), 50, 1000);
        if ((v = yyjson_obj_get(local, "burst_debounce_ms")) && yyjson_is_int(v)) cfg.local.burst_debounce_ms = std::clamp(yyjson_get_int(v), 0, 2000);
        if ((v = yyjson_obj_get(local, "initial_scan_seconds")) && yyjson_is_int(v)) cfg.local.initial_scan_seconds = std::clamp(yyjson_get_int(v), 0, 10);
    }

    // appearance
    yyjson_val* app = yyjson_obj_get(root, "appearance");
    if (app && yyjson_is_obj(app)) {
        if ((v = yyjson_obj_get(app, "dark_mode")) && yyjson_is_bool(v)) cfg.appearance.dark_mode = yyjson_get_bool(v);
    }

    yyjson_doc_free(doc);
    return cfg;
}

Config Config::LoadFromFile(const std::wstring& path) {
    if (!Utils::FileExists(path)) {
        Config def = GetDefault();
        def.SaveToFile(path);
        return def;
    }
    std::string pathUtf8 = Utils::WideToUtf8(path);
    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_file(pathUtf8.c_str(), 0, nullptr, &err);
    if (!doc) {
        return GetDefault();
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    std::string jsonStr;
    if (root) {
        char* json = yyjson_val_write(root, 0, nullptr);
        if (json) {
            jsonStr = json;
            free(json);
        }
    }
    yyjson_doc_free(doc);
    return LoadFromString(jsonStr);
}

bool Config::SaveToFile(const std::wstring& path) const {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "detection_mode", "local");
    yyjson_mut_obj_add_bool(doc, root, "enabled", enabled);
    yyjson_mut_obj_add_bool(doc, root, "notify_enemies", notify_enemies);
    yyjson_mut_obj_add_bool(doc, root, "notify_allies", notify_allies);
    yyjson_mut_obj_add_str(doc, root, "members_file", "players.json");
    std::string watchlistUrlUtf8 = Utils::WideToUtf8(watchlist_url);
    yyjson_mut_obj_add_strcpy(doc, root, "watchlist_url", watchlistUrlUtf8.c_str());
    std::string updateUrlUtf8 = Utils::WideToUtf8(update_url);
    yyjson_mut_obj_add_strcpy(doc, root, "update_url", updateUrlUtf8.c_str());

    // sound
    yyjson_mut_val* vSound = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, vSound, "enabled", sound.enabled);
    yyjson_mut_obj_add_bool(doc, vSound, "enemies_only", sound.enemies_only);
    yyjson_mut_obj_add_real(doc, vSound, "volume", sound.volume);
    std::string soundFileUtf8 = Utils::WideToUtf8(sound.enemy_file.empty() ? sound.file : sound.enemy_file);
    std::string enemyFileUtf8 = Utils::WideToUtf8(sound.enemy_file.empty() ? sound.file : sound.enemy_file);
    std::string allyFileUtf8 = Utils::WideToUtf8(sound.ally_file.empty() ? L"ping.wav" : sound.ally_file);
    yyjson_mut_obj_add_strcpy(doc, vSound, "file", soundFileUtf8.c_str());
    yyjson_mut_obj_add_strcpy(doc, vSound, "enemy_file", enemyFileUtf8.c_str());
    yyjson_mut_obj_add_strcpy(doc, vSound, "ally_file", allyFileUtf8.c_str());
    yyjson_mut_obj_add_val(doc, root, "sound", vSound);

    // scan
    yyjson_mut_val* vScan = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, vScan, "interval_ms", 1200);
    yyjson_mut_obj_add_int(doc, vScan, "confirmations", 2);
    yyjson_mut_obj_add_int(doc, vScan, "scale", 2);
    yyjson_mut_obj_add_bool(doc, vScan, "only_when_roblox_foreground", true);
    yyjson_mut_obj_add_int(doc, vScan, "repeat_after_seconds", 0);
    yyjson_mut_obj_add_null(doc, vScan, "region");
    yyjson_mut_obj_add_val(doc, root, "scan", vScan);

    // notification
    yyjson_mut_val* vNotif = yyjson_mut_obj(doc);
    std::string cornerUtf8 = Utils::WideToUtf8(notification.corner);
    yyjson_mut_obj_add_str(doc, vNotif, "corner", cornerUtf8.c_str());
    yyjson_mut_obj_add_real(doc, vNotif, "duration_seconds", notification.duration_seconds);
    yyjson_mut_obj_add_int(doc, vNotif, "width", notification.width);
    yyjson_mut_obj_add_int(doc, vNotif, "margin", notification.margin);
    yyjson_mut_obj_add_real(doc, vNotif, "opacity", notification.opacity);
    std::string bgUtf8 = Utils::WideToUtf8(notification.background);
    yyjson_mut_obj_add_str(doc, vNotif, "background", bgUtf8.c_str());
    std::string txtUtf8 = Utils::WideToUtf8(notification.text);
    yyjson_mut_obj_add_str(doc, vNotif, "text", txtUtf8.c_str());
    std::string eaUtf8 = Utils::WideToUtf8(notification.enemy_accent);
    yyjson_mut_obj_add_str(doc, vNotif, "enemy_accent", eaUtf8.c_str());
    std::string aaUtf8 = Utils::WideToUtf8(notification.ally_accent);
    yyjson_mut_obj_add_str(doc, vNotif, "ally_accent", aaUtf8.c_str());
    yyjson_mut_obj_add_bool(doc, vNotif, "show_clan", notification.show_clan);
    yyjson_mut_obj_add_int(doc, vNotif, "max_visible", notification.max_visible);
    yyjson_mut_obj_add_val(doc, root, "notification", vNotif);

    // local
    yyjson_mut_val* vLoc = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, vLoc, "poll_interval_ms", local.poll_interval_ms);
    yyjson_mut_obj_add_int(doc, vLoc, "burst_debounce_ms", local.burst_debounce_ms);
    yyjson_mut_obj_add_int(doc, vLoc, "initial_scan_seconds", local.initial_scan_seconds);
    yyjson_mut_obj_add_val(doc, root, "local", vLoc);

    // appearance
    yyjson_mut_val* vApp = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, vApp, "dark_mode", appearance.dark_mode);
    yyjson_mut_obj_add_val(doc, root, "appearance", vApp);

    std::string pathUtf8 = Utils::WideToUtf8(path);
    std::string tmpPathUtf8 = pathUtf8 + ".tmp";
    yyjson_write_err err;
    bool ok = yyjson_mut_write_file(tmpPathUtf8.c_str(), doc, YYJSON_WRITE_PRETTY, nullptr, &err);
    yyjson_mut_doc_free(doc);

    if (ok) {
        std::wstring tmpPath = path + L".tmp";
        MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        return true;
    }
    return false;
}
