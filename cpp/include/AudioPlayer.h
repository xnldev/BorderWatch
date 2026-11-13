#pragma once
#include <string>
#include <vector>
#include <memory>

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool Initialize();
    void Shutdown();

    bool Play(const std::wstring& soundFile, double volume, const std::wstring& rootDir);
    bool TestAudioFile(const std::wstring& soundFile, std::wstring& outError);
    std::wstring ImportCustomSound(const std::wstring& srcPath, const std::wstring& rootDir, std::wstring& outError);

private:
    void EnsureDefaultSoundsExtracted(const std::wstring& rootDir);
    std::wstring ResolveSoundPath(const std::wstring& soundFile, const std::wstring& rootDir);

    void* m_engine = nullptr; // ptr to ma_engine
    bool m_initialized = false;

    struct SoundInstance;
    std::vector<std::unique_ptr<SoundInstance>> m_activeSounds;
};
