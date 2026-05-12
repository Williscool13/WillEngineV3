//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_CONFIG_H
#define WILL_ENGINE_ASSET_MANAGER_CONFIG_H
#include <cstdint>

namespace Engine
{
inline constexpr uint32_t MAX_LOADED_MODELS = 2048;
inline constexpr uint32_t MAX_LOADED_TEXTURES = 2048;
inline constexpr uint32_t MAX_LOADED_SAMPLERS = 128;
inline constexpr uint32_t MAX_LOADED_CUBEMAPS = 256;
inline constexpr uint32_t MAX_LOADED_AUDIO = 128;
inline constexpr uint32_t MAX_LOADED_FONTS = 64;

inline constexpr uint32_t MAX_CACHED_MODELS = 4096;
inline constexpr uint32_t MAX_CACHED_TEXTURES = 4096;
inline constexpr uint32_t MAX_CACHED_SAMPLERS = 256;
inline constexpr uint32_t MAX_CACHED_CUBEMAPS = 512;
inline constexpr uint32_t MAX_CACHED_AUDIO = 256;
inline constexpr uint32_t MAX_CACHED_SCENES = 512;
inline constexpr uint32_t MAX_CACHED_PREFABS = 512;
inline constexpr uint32_t MAX_CACHED_FONTS = 256;


inline constexpr uint32_t MAX_LOADED_MATERIALS = 4096;

inline constexpr uint32_t ASSET_LOG_IDLE_SECONDS = 1;

}

#endif //WILL_ENGINE_ASSET_MANAGER_CONFIG_H