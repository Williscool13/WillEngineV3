//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_AUDIO_ASSET_H
#define WILL_ENGINE_AUDIO_ASSET_H
#include <filesystem>

#include "engine/asset_manager_types.h"
#include "SDL3_mixer/SDL_mixer.h"

namespace Audio
{
struct WillAudio
{
    enum class AudioLoadState
    {
        NotLoaded,
        Loaded,
        FailedToLoad
    };

    std::filesystem::path source{};
    std::string name{};
    Engine::AudioHandle selfHandle;
    AudioLoadState loadState{AudioLoadState::NotLoaded};
    uint32_t refCount = 0;

    // Populated in AssetLoadThread
    MIX_Audio* mixAudio = nullptr;
};
} // Audio

#endif //WILL_ENGINE_AUDIO_ASSET_H
