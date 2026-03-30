//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_AUDIO_MANAGER_H
#define WILL_ENGINE_AUDIO_MANAGER_H
#include <array>
#include <memory>
#include <string>
#include <unordered_map>

#include "audio_asset.h"
#include "audio_config.h"
#include "core/memory/handle.h"
#include "core/memory/handle_allocator.h"
#include "SDL3_mixer/SDL_mixer.h"

namespace AssetLoad
{
class AsyncAssetLoadManager;
}

namespace Audio
{
class AudioManager
{
public:
    AudioManager(AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager);

    ~AudioManager();

    static void RegisterAudio();

    void Update() const;

    Core::Handle<WillAudio> LoadAudio(const std::string& name, const std::filesystem::path& path);

    void UnloadAudio(Core::Handle<WillAudio> handle);

    void PlayMusic(Core::Handle<WillAudio> handle, bool loop = true);
    void PlaySfx(Core::Handle<WillAudio> handle);

    void SetMusicVolume(float volume);

    void StopMusic();

    void SetMasterVolume(float volume);


    Core::Handle<WillAudio> GetAudio(const std::string& name)
    {
        const auto it = namedAudio.find(name);
        return it != namedAudio.end() ? it->second : Core::Handle<WillAudio>::INVALID;
    }

    WillAudio* GetAudioPtr(Core::Handle<WillAudio> handle)
    {
        return audioHandleAllocator.IsValid(handle) ? &audioAssets[handle.index] : nullptr;
    }

private:
    AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager;

    MIX_Mixer* mixer = nullptr;
    MIX_Track* musicMixerTrack = nullptr;
    std::array<MIX_Track*, MAX_SFX_TRACKS> sfxTracks{};
    uint32_t currentSfxIndex = 0;

    SDL_PropertiesID infiniteMusicProp;

    Core::HandleAllocator<WillAudio, 256> audioHandleAllocator;
    std::array<WillAudio, 256> audioAssets;

    std::unordered_map<std::string, Core::Handle<WillAudio> > namedAudio;
};
} // Audio

#endif //WILL_ENGINE_AUDIO_MANAGER_H
