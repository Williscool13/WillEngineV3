//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_CONFIG_H
#define WILL_ENGINE_ASSET_LOAD_CONFIG_H
#include <cstdint>

namespace AssetLoad
{
inline constexpr uint32_t MAX_ASSET_LOAD_JOB_COUNT = 64;

inline constexpr uint32_t WILL_MODEL_JOB_COUNT = 4;
inline constexpr uint32_t WILL_MODEL_LOAD_QUEUE_COUNT = 64;
inline constexpr uint32_t TEXTURE_JOB_COUNT = 16;
inline constexpr uint32_t TEXTURE_LOAD_QUEUE_COUNT = 128;

inline constexpr uint32_t WILL_MODEL_LOAD_STAGING_SIZE = 32 * 1024 * 1024; // 32MB
inline constexpr uint32_t TEXTURE_LOAD_STAGING_SIZE    = 12 * 1024 * 1024; // 12MB, BC7 compressed is ~10.7MB mip 0 4k texture

inline constexpr uint32_t DEFAULT_SAMPLER_BINDLESS_INDEX = 0;
inline constexpr uint32_t WHITE_IMAGE_BINDLESS_INDEX = 0;
inline constexpr uint32_t ERROR_IMAGE_BINDLESS_INDEX = 1;
}

#endif //WILL_ENGINE_ASSET_LOAD_CONFIG_H