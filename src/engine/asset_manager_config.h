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
inline constexpr uint32_t MAX_LOADED_COLLIDERS = 2048;
// Side Pools
inline constexpr uint32_t MAX_LOADED_MODULE_MODELS = 256;
inline constexpr uint32_t MAX_POOLED_SPLINE_PARAMS = 256;
inline constexpr uint32_t MAX_POOLED_TEXT3D_PARAMS = 256;

inline constexpr uint32_t MAX_CACHED_MODELS = 4096;
inline constexpr uint32_t MAX_CACHED_TEXTURES = 4096;
inline constexpr uint32_t MAX_CACHED_SAMPLERS = 256;
inline constexpr uint32_t MAX_CACHED_CUBEMAPS = 512;
inline constexpr uint32_t MAX_CACHED_PROBES = 512;
inline constexpr uint32_t MAX_CACHED_AUDIO = 256;
inline constexpr uint32_t MAX_CACHED_SCENES = 512;
inline constexpr uint32_t MAX_CACHED_PREFABS = 512;
inline constexpr uint32_t MAX_CACHED_FONTS = 256;
inline constexpr uint32_t MAX_STATIC_PROCEDURAL_TEXTURES = 64;


inline constexpr uint32_t MAX_LOADED_MATERIALS = 4096;
inline constexpr uint32_t MAX_LOADED_TEXT_MATERIALS = 256;

inline constexpr uint32_t ASSET_LOG_IDLE_SECONDS = 1;

inline constexpr uint64_t TEXTURE_RETIRE_PENDING = UINT64_MAX;
inline constexpr uint64_t MODEL_RETIRE_PENDING = UINT64_MAX;
inline constexpr uint64_t FONT_RETIRE_PENDING = UINT64_MAX;
inline constexpr uint64_t COLLIDER_RETIRE_PENDING = UINT64_MAX;
inline constexpr uint64_t CUBEMAP_RETIRE_PENDING = UINT64_MAX;

}

#endif //WILL_ENGINE_ASSET_MANAGER_CONFIG_H