//
// Created by William on 2026-01-26.
//

#include "audio_manager.h"

#include "audio_asset.h"
#include "asset-load/async_asset_load_manager.h"
#include "platform/paths.h"
#include "SDL_mixer/include/SDL3_mixer/SDL_mixer.h"
#include "spdlog/spdlog.h"

namespace Audio
{
AudioManager::AudioManager(AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager)
    : asyncAssetLoadManager(asyncAssetLoadManager)
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



    Core::Handle<WillAudio> audioHandle = audioHandleAllocator.Add();
    if (!audioHandle.IsValid()) {
        SPDLOG_ERROR("Failed to allocate audio handle");
        return;
    }

    WillAudio& testAudio = audioAssets[audioHandle.index];
    testAudio.name = "test_music";
    testAudio.source = musicPath;
    testAudio.mixer = mixer;
    testAudio.mixAudio = nullptr;

    namedAudio["test_music"] = audioHandle;


    SPDLOG_INFO("Requesting async load for: {} (handle: {}/{})", musicPath.string(), audioHandle.index, audioHandle.generation);
    asyncAssetLoadManager->RequestAudioLoad(&testAudio);

    SPDLOG_INFO("Active async loads: {}", asyncAssetLoadManager->GetActiveLoadCount());
}

AudioManager::~AudioManager()
{
    MIX_StopTrack(musicMixerTrack, MIX_TrackMSToFrames(musicMixerTrack, 1000));
    MIX_DestroyTrack(musicMixerTrack);

    MIX_DestroyAudio(music);
    MIX_DestroyMixer(mixer);
    MIX_Quit();
}

void AudioManager::Update()
{
    asyncAssetLoadManager->ProcessCompletedLoads();
}
} // Audio
