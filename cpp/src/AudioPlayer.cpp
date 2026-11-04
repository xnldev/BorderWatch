#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio.h"
#include "../include/AudioPlayer.h"
#include "../include/Utils.h"
#include "../include/Resource.h"
#include <fstream>
#include <filesystem>

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
    if (m_initialized && m_engine) {
        ma_engine* engine = static_cast<ma_engine*>(m_engine);
        ma_engine_uninit(engine);
        delete engine;
        m_engine = nullptr;
        m_initialized = false;
    }
}

void AudioPlayer::EnsureDefaultSoundsExtracted(const std::wstring& rootDir) {
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
                std::string pathUtf8 = Utils::WideToUtf8(path);
                std::ofstream out(pathUtf8, std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                }
            }
        }
    }
}

bool AudioPlayer::Play(const std::wstring& soundFile, double volume, const std::wstring& rootDir) {
    if (!Initialize()) return false;

    EnsureDefaultSoundsExtracted(rootDir);

    std::wstring fullPath = soundFile;
    if (soundFile.find(L':') == std::wstring::npos && soundFile.find(L'\\') == std::wstring::npos) {
        // look in rootDir first then rootDir/sounds
        fullPath = Utils::CombinePath(rootDir, soundFile);
        if (!Utils::FileExists(fullPath)) {
            fullPath = Utils::CombinePath(Utils::CombinePath(rootDir, L"sounds"), soundFile);
        }
    }

    if (!Utils::FileExists(fullPath)) return false;

    std::string pathUtf8 = Utils::WideToUtf8(fullPath);
    ma_engine* engine = static_cast<ma_engine*>(m_engine);
    ma_engine_set_volume(engine, (float)volume);
    ma_result res = ma_engine_play_sound(engine, pathUtf8.c_str(), nullptr);
    return res == MA_SUCCESS;
}

bool AudioPlayer::TestAudioFile(const std::wstring& soundFile, std::wstring& outError) {
    if (!Utils::FileExists(soundFile)) {
        outError = L"File does not exist: " + soundFile;
        return false;
    }

    std::string pathUtf8 = Utils::WideToUtf8(soundFile);
    ma_decoder decoder;
    ma_result res = ma_decoder_init_file(pathUtf8.c_str(), nullptr, &decoder);
    if (res != MA_SUCCESS) {
        outError = L"Could not decode audio file (error code " + std::to_wstring(res) + L").";
        return false;
    }
    ma_decoder_uninit(&decoder);
    return true;
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

    // hash based on file size and bytes
    std::string srcUtf8 = Utils::WideToUtf8(srcPath);
    std::ifstream in(srcUtf8, std::ios::binary);
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

    // FNV-1a
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
        std::string destUtf8 = Utils::WideToUtf8(destPath);
        std::ofstream out(destUtf8, std::ios::binary);
        if (!out.is_open()) {
            outError = L"Could not copy sound file to data directory.";
            return L"";
        }
        out.write(buf.data(), size);
        out.close();
    }

    return Utils::CombinePath(L"sounds", destFileName);
}
