#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio.h"
#include "../include/AudioPlayer.h"
#include "../include/Utils.h"
#include "../include/Resource.h"
#include <fstream>
#include <filesystem>
#include <algorithm>

struct AudioPlayer::SoundInstance {
    ma_sound sound;
    bool initialized = false;

    ~SoundInstance() {
        if (initialized) {
            ma_sound_stop(&sound);
            ma_sound_uninit(&sound);
            initialized = false;
        }
    }
};

AudioPlayer::AudioPlayer() {}

AudioPlayer::~AudioPlayer() {
    Shutdown();
}

bool AudioPlayer::Initialize() {
    if (m_initialized) return true;

    ma_engine* engine = new ma_engine();
    ma_result result = ma_engine_init(nullptr, engine);
    if (result != MA_SUCCESS) {
        delete engine;
        return false;
    }

    m_engine = engine;
    m_initialized = true;
    return true;
}

void AudioPlayer::Shutdown() {
    m_activeSounds.clear();
    if (m_initialized && m_engine) {
        ma_engine* engine = static_cast<ma_engine*>(m_engine);
        ma_engine_uninit(engine);
        delete engine;
        m_engine = nullptr;
        m_initialized = false;
    }
}

void AudioPlayer::EnsureDefaultSoundsExtracted(const std::wstring& rootDir) {
    if (rootDir.empty()) return;
    std::wstring soundDir = Utils::CombinePath(rootDir, L"sounds");
    Utils::EnsureDirectoryExists(soundDir);

    struct SoundAsset {
        int id;
        std::wstring filename;
    } assets[] = {
        { IDR_SOUND_MEOW_1, L"meow-1.mp3" },
        { IDR_SOUND_PING, L"ping.wav" },
        { IDR_SOUND_MEOW_SOFT, L"meow-soft.wav" },
        { IDR_SOUND_MEOW, L"meow.wav" }
    };

    for (const auto& a : assets) {
        std::wstring path = Utils::CombinePath(rootDir, a.filename);
        if (!Utils::FileExists(path)) {
            std::vector<uint8_t> bytes = Utils::LoadResourceBytes(a.id);
            if (!bytes.empty()) {
                std::ofstream out(path.c_str(), std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                }
            }
        }
        std::wstring pathInSounds = Utils::CombinePath(soundDir, a.filename);
        if (!Utils::FileExists(pathInSounds)) {
            std::vector<uint8_t> bytes = Utils::LoadResourceBytes(a.id);
            if (!bytes.empty()) {
                std::ofstream out(pathInSounds.c_str(), std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                }
            }
        }
    }
}

std::wstring AudioPlayer::ResolveSoundPath(const std::wstring& soundFile, const std::wstring& rootDir) {
    if (soundFile.empty()) return L"";

    if (Utils::FileExists(soundFile)) {
        return soundFile;
    }

    if (!rootDir.empty()) {
        std::wstring candidate = Utils::CombinePath(rootDir, soundFile);
        if (Utils::FileExists(candidate)) {
            return candidate;
        }

        std::wstring filenameOnly = soundFile;
        size_t lastSlash = soundFile.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            filenameOnly = soundFile.substr(lastSlash + 1);
        }

        std::wstring inSounds = Utils::CombinePath(Utils::CombinePath(rootDir, L"sounds"), filenameOnly);
        if (Utils::FileExists(inSounds)) {
            return inSounds;
        }
    }

    std::wstring exeDir = Utils::GetExecutableDirectory();
    if (!exeDir.empty()) {
        std::wstring candidate = Utils::CombinePath(exeDir, soundFile);
        if (Utils::FileExists(candidate)) return candidate;

        std::wstring inSounds = Utils::CombinePath(Utils::CombinePath(exeDir, L"sounds"), soundFile);
        if (Utils::FileExists(inSounds)) return inSounds;
    }

    return L"";
}

bool AudioPlayer::Play(const std::wstring& soundFile, double volume, const std::wstring& rootDir) {
    if (!Initialize()) return false;

    EnsureDefaultSoundsExtracted(rootDir);

    std::wstring fullPath = ResolveSoundPath(soundFile, rootDir);
    if (fullPath.empty() || !Utils::FileExists(fullPath)) {
        return false;
    }

    for (auto it = m_activeSounds.begin(); it != m_activeSounds.end(); ) {
        if (!(*it)->initialized || ma_sound_at_end(&(*it)->sound)) {
            it = m_activeSounds.erase(it);
        } else {
            ++it;
        }
    }

    if (m_activeSounds.size() > 8) {
        m_activeSounds.erase(m_activeSounds.begin());
    }

    ma_engine* engine = static_cast<ma_engine*>(m_engine);
    auto instance = std::make_unique<SoundInstance>();
    ma_result res = ma_sound_init_from_file_w(engine, fullPath.c_str(), MA_SOUND_FLAG_ASYNC, nullptr, nullptr, &instance->sound);
    if (res == MA_SUCCESS) {
        instance->initialized = true;
        ma_sound_set_volume(&instance->sound, static_cast<float>(volume));
        ma_result startRes = ma_sound_start(&instance->sound);
        if (startRes == MA_SUCCESS) {
            m_activeSounds.push_back(std::move(instance));
            return true;
        }
    }

    std::wstring ext = fullPath;
    size_t dot = ext.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = ext.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    if (ext == L".wav") {
        if (PlaySoundW(fullPath.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
            return true;
        }
    } else if (ext == L".mp3") {
        static int s_mciCounter = 0;
        std::wstring alias = L"bw_snd_" + std::to_wstring(++s_mciCounter % 16);
        std::wstring closeCmd = L"close " + alias;
        mciSendStringW(closeCmd.c_str(), nullptr, 0, nullptr);
        std::wstring openCmd = L"open \"" + fullPath + L"\" type mpegvideo alias " + alias;
        if (mciSendStringW(openCmd.c_str(), nullptr, 0, nullptr) == 0) {
            std::wstring playCmd = L"play " + alias + L" from 0";
            if (mciSendStringW(playCmd.c_str(), nullptr, 0, nullptr) == 0) {
                return true;
            }
        }
    }

    return false;
}

bool AudioPlayer::TestAudioFile(const std::wstring& soundFile, std::wstring& outError) {
    if (!Utils::FileExists(soundFile)) {
        outError = L"File does not exist: " + soundFile;
        return false;
    }

    ma_decoder decoder;
    ma_result res = ma_decoder_init_file_w(soundFile.c_str(), nullptr, &decoder);
    if (res == MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return true;
    }

    std::ifstream in(soundFile.c_str(), std::ios::binary);
    if (in.is_open()) {
        char header[4] = { 0 };
        in.read(header, 4);
        in.close();
        if (memcmp(header, "RIFF", 4) == 0) return true;
        if (memcmp(header, "ID3", 3) == 0) return true;
        if ((uint8_t)header[0] == 0xFF && ((uint8_t)header[1] & 0xE0) == 0xE0) return true;
    }

    outError = L"Could not decode audio file (error code " + std::to_wstring(res) + L").";
    return false;
}

std::wstring AudioPlayer::ImportCustomSound(const std::wstring& srcPath, const std::wstring& rootDir, std::wstring& outError) {
    std::wstring ext;
    size_t dot = srcPath.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = srcPath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }
    if (ext != L".mp3" && ext != L".wav") {
        outError = L"Only MP3 and WAV files are supported.";
        return L"";
    }

    if (!TestAudioFile(srcPath, outError)) {
        return L"";
    }

    std::wstring soundDir = Utils::CombinePath(rootDir, L"sounds");
    Utils::EnsureDirectoryExists(soundDir);

    std::ifstream in(srcPath.c_str(), std::ios::binary);
    if (!in.is_open()) {
        outError = L"Could not open source audio file.";
        return L"";
    }

    in.seekg(0, std::ios::end);
    size_t size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);

    if (size == 0 || size > 25 * 1024 * 1024) {
        outError = L"Sound file must be non-empty and smaller than 25 MB.";
        return L"";
    }

    std::vector<char> buf(size);
    in.read(buf.data(), size);
    in.close();

    uint64_t hash = 14695981039346656037ULL;
    for (char c : buf) {
        hash ^= (uint8_t)c;
        hash *= 1099511628211ULL;
    }

    wchar_t hexBuf[32];
    swprintf_s(hexBuf, L"%016llx", hash);
    std::wstring destFileName = std::wstring(hexBuf) + ext;
    std::wstring destPath = Utils::CombinePath(soundDir, destFileName);

    if (!Utils::FileExists(destPath)) {
        std::ofstream out(destPath.c_str(), std::ios::binary);
        if (!out.is_open()) {
            outError = L"Could not copy sound file to data directory.";
            return L"";
        }
        out.write(buf.data(), size);
        out.close();
    }

    return Utils::CombinePath(L"sounds", destFileName);
}
