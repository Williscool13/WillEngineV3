//
// Created by William on 2026-01-26.
//

#include "audio_manager.h"

#include "platform/paths.h"
#include "SDL_mixer/include/SDL3_mixer/SDL_mixer.h"
#include "spdlog/spdlog.h"

namespace Audio
{
AudioManager::AudioManager()
{
    MIX_Init();

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (mixer == nullptr) {
        // todo: graceful audio fail
        return;
    }

    std::filesystem::path musicPath = Platform::GetAssetPath() / "audio/the_entertainer.ogg";
    music = MIX_LoadAudio(mixer, musicPath.string().c_str(), false);
    if (music == nullptr) {
        return;
    }

    musicMixerTrack = MIX_CreateTrack(mixer);
    if (musicMixerTrack == nullptr) {
        SPDLOG_ERROR("Failed to create music track: {}", SDL_GetError());
        return;
    }
    MIX_SetTrackAudio(musicMixerTrack, music);
    MIX_PlayTrack(musicMixerTrack, NULL);
}

AudioManager::~AudioManager()
{
    MIX_StopTrack(musicMixerTrack, MIX_TrackMSToFrames(musicMixerTrack, 1000));
    MIX_DestroyTrack(musicMixerTrack);

    MIX_DestroyAudio(music);
    MIX_DestroyMixer(mixer);
    MIX_Quit();
}
} // Audio
