//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_AUDIO_MANAGER_H
#define WILL_ENGINE_AUDIO_MANAGER_H

#include "audio_asset.h"
#include "audio_config.h"
#include "core/string_id.h"
#include "core/containers/array.h"
#include "core/containers/heap_array.h"
#include "core/containers/inline_map.h"
#include "core/containers/inline_path.h"
#include "core/memory/handle.h"
#include "core/memory/handle_allocator.h"
#include "core/memory/memory_manager.h"
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

    Core::Handle<WillAudio> LoadAudio(const char* name, const Core::Path& path);

    void UnloadAudio(Core::Handle<WillAudio> handle);

    void PlayMusic(Core::Handle<WillAudio> handle, bool loop = true);

    void PlaySfx(Core::Handle<WillAudio> handle);

    void SetMusicVolume(float volume);

    void StopMusic();

    void SetMasterVolume(float volume);


    Core::Handle<WillAudio> GetAudio(const char* name)
    {
        const Core::Handle<WillAudio>* found = namedAudio.Find(StringID{name, strlen(name)});
        return found ? *found : Core::Handle<WillAudio>::INVALID;
    }

    WillAudio* GetAudioPtr(Core::Handle<WillAudio> handle)
    {
        return audioHandleAllocator.IsValid(handle) ? &audioAssets[handle.index] : nullptr;
    }

private:
    AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager;

    MIX_Mixer* mixer = nullptr;
    MIX_Track* musicMixerTrack = nullptr;
    Core::Array<MIX_Track*, MAX_SFX_TRACKS> sfxTracks{};
    uint32_t currentSfxIndex = 0;

    SDL_PropertiesID infiniteMusicProp;

    Core::HandleAllocator<WillAudio, 256> audioHandleAllocator;
    Core::Array<WillAudio, 256> audioAssets;

    Core::InlineMap<StringID, Core::Handle<WillAudio>, 512> namedAudio;
};
} // Audio

#endif //WILL_ENGINE_AUDIO_MANAGER_H
