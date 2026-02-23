//
// Created by William on 2026-01-26.
//

#include "audio_manager.h"

#include "audio_asset.h"
#include "asset-load/async_asset_load_manager.h"
#include "engine/logging/engine_log.h"
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

    musicMixerTrack = MIX_CreateTrack(mixer);
    if (musicMixerTrack == nullptr) {
        LOG_ERROR(Audio, "Failed to create music track: {}", SDL_GetError());
        return;
    }

    for (size_t i = 0; i < MAX_SFX_TRACKS; ++i) {
        sfxTracks[i] = MIX_CreateTrack(mixer);
        if (!sfxTracks[i]) {
            LOG_ERROR(Audio, "Failed to create SFX track {}: {}", i, SDL_GetError());
        }
    }

    infiniteMusicProp = SDL_CreateProperties();
    SDL_SetNumberProperty(infiniteMusicProp, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    // SetMusicVolume(0.5f);

    std::filesystem::path musicPath = Platform::GetAssetPath() / "audio/the_entertainer.ogg";
    Core::Handle<WillAudio> audioHandle = LoadAudio("test_music", musicPath);
    LOG_INFO(Audio, "Requesting async load for: {} (handle: {}/{})", musicPath.string(), audioHandle.index, audioHandle.generation);
}

AudioManager::~AudioManager()
{
    if (musicMixerTrack) {
        MIX_StopTrack(musicMixerTrack, 0);
        MIX_DestroyTrack(musicMixerTrack);
    }

    for (auto* track : sfxTracks) {
        if (track) {
            MIX_StopTrack(track, 0);
            MIX_DestroyTrack(track);
        }
    }

    for (auto& audio : audioAssets) {
        if (audio.mixAudio) {
            MIX_DestroyAudio(audio.mixAudio);
            audio.mixAudio = nullptr;
        }
    }

    if (infiniteMusicProp) {
        SDL_DestroyProperties(infiniteMusicProp);
    }

    if (mixer) {
        MIX_DestroyMixer(mixer);
    }

    MIX_Quit();
}

void AudioManager::RegisterAudio()
{
    MIX_Init();
}

void AudioManager::Update() const
{
    AssetLoad::AudioLoadComplete complete;
    while (asyncAssetLoadManager->TryDequeueAudioComplete(complete)) {
        if (complete.bSuccess) {
            complete.audioEntry->loadState = WillAudio::AudioLoadState::Loaded;
            LOG_INFO(Audio, "Audio loaded: {}", complete.audioEntry->name);
        }
        else {
            complete.audioEntry->loadState = WillAudio::AudioLoadState::FailedToLoad;
            LOG_ERROR(Audio, "Audio load failed: {}", complete.audioEntry->name);
        }
    }
}

Core::Handle<WillAudio> AudioManager::LoadAudio(const std::string& name, const std::filesystem::path& path)
{
    auto it = namedAudio.find(name);
    if (it != namedAudio.end()) {
        WillAudio* audio = GetAudioPtr(it->second);
        if (audio) {
            audio->refCount++;
            LOG_INFO(Audio, "Incremented ref count for audio '{}' to {}", name, audio->refCount);
        }
        return it->second;
    }

    Core::Handle<WillAudio> handle = audioHandleAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Audio, "Failed to allocate handle for audio: {}", name);
        return Core::Handle<WillAudio>::INVALID;
    }

    WillAudio& audio = audioAssets[handle.index];
    audio.name = name;
    audio.source = path;
    audio.mixer = mixer;
    audio.selfHandle = Engine::AudioHandle{handle.index, handle.generation};
    audio.loadState = WillAudio::AudioLoadState::NotLoaded;
    audio.refCount = 1;

    namedAudio[name] = handle;
    asyncAssetLoadManager->RequestAudioLoad(&audio);

    LOG_INFO(Audio, "Loading new audio '{}' with ref count 1", name);

    return handle;
}

void AudioManager::UnloadAudio(Core::Handle<WillAudio> handle)
{
    WillAudio* audio = GetAudioPtr(handle);
    if (!audio) {
        LOG_ERROR(Audio, "Invalid audio handle for unload");
        return;
    }

    if (audio->refCount > 0) {
        audio->refCount--;
        LOG_INFO(Audio, "Decremented ref count for audio '{}' to {}", audio->name, audio->refCount);
    }

    if (audio->refCount == 0) {
        // TODO: Actually unload audio when needed
        LOG_INFO(Audio, "Audio '{}' ref count reached 0 (unload not implemented yet)", audio->name);
    }
}

void AudioManager::PlayMusic(Core::Handle<WillAudio> handle, bool loop)
{
    WillAudio* audio = GetAudioPtr(handle);
    if (!audio) {
        LOG_ERROR(Audio, "Invalid audio handle for music playback");
        return;
    }

    if (audio->loadState != WillAudio::AudioLoadState::Loaded) {
        LOG_WARN(Audio, "Audio not loaded yet: {}", audio->name);
        return;
    }

    if (!audio->mixAudio) {
        LOG_ERROR(Audio, "Audio data is null for: {}", audio->name);
        return;
    }

    if (musicMixerTrack) {
        MIX_StopTrack(musicMixerTrack, 0);
    }
    else {
        musicMixerTrack = MIX_CreateTrack(mixer);
        if (!musicMixerTrack) {
            LOG_ERROR(Audio, "Failed to create music track: {}", SDL_GetError());
            return;
        }
    }

    MIX_SetTrackAudio(musicMixerTrack, audio->mixAudio);

    bool res = MIX_PlayTrack(musicMixerTrack, loop ? infiniteMusicProp : 0);
    if (!res) {
        LOG_INFO(Audio, "Failed to play track");
    }
}

void AudioManager::PlaySfx(Core::Handle<WillAudio> handle)
{
    WillAudio* audio = GetAudioPtr(handle);
    if (!audio || audio->loadState != WillAudio::AudioLoadState::Loaded || !audio->mixAudio) {
        LOG_ERROR(Audio, "Cannot play SFX: invalid audio state");
        return;
    }

    MIX_Track* currentTrack = sfxTracks[currentSfxIndex];
    MIX_SetTrackAudio(currentTrack, audio->mixAudio);
    MIX_PlayTrack(currentTrack, 0);
    currentSfxIndex += 1;
    currentSfxIndex %= MAX_SFX_TRACKS;
}

void AudioManager::SetMusicVolume(float volume)
{
    if (musicMixerTrack) {
        MIX_SetTrackGain(musicMixerTrack, volume);
    }
}


void AudioManager::StopMusic()
{
    if (musicMixerTrack) {
        MIX_StopTrack(musicMixerTrack, MIX_TrackMSToFrames(musicMixerTrack, 500));
    }
}

void AudioManager::SetMasterVolume(float volume)
{
    if (mixer) {
        MIX_SetMixerGain(mixer, volume);
    }
}
} // Audio
