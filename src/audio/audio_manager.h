//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_AUDIO_MANAGER_H
#define WILL_ENGINE_AUDIO_MANAGER_H
#include "SDL3_mixer/SDL_mixer.h"

namespace Audio
{
class AudioManager
{
public:
    AudioManager();
    ~AudioManager();

private:
    MIX_Mixer* mixer = nullptr;
    MIX_Track* musicMixerTrack = nullptr;
    MIX_Audio* music = nullptr;
};
} // Audio

#endif //WILL_ENGINE_AUDIO_MANAGER_H