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
#include "core/allocators/handle.h"
#include "core/allocators/handle_allocator.h"
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

    void Update();

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
    MIX_Audio* music = nullptr;

    Core::HandleAllocator<WillAudio, 256> audioHandleAllocator;
    std::array<WillAudio, 256> audioAssets;

    std::unordered_map<std::string, Core::Handle<WillAudio> > namedAudio;
};
} // Audio

#endif //WILL_ENGINE_AUDIO_MANAGER_H
