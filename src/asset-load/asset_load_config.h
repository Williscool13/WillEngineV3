//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_CONFIG_H
#define WILL_ENGINE_ASSET_LOAD_CONFIG_H
#include <cstdint>

namespace AssetLoad
{
inline constexpr uint32_t MAX_ASSET_LOAD_JOB_COUNT = 64;

inline constexpr uint32_t WILL_MODEL_JOB_COUNT = 2;
inline constexpr uint32_t WILL_MODEL_LOAD_QUEUE_COUNT = 64;
inline constexpr uint32_t TEXTURE_JOB_COUNT = 4;
inline constexpr uint32_t TEXTURE_LOAD_QUEUE_COUNT = 128;
inline constexpr uint32_t PIPELINE_JOB_COUNT = 8;
inline constexpr uint32_t PIPELINE_LOAD_QUEUE_COUNT = 32;

inline constexpr uint32_t WILL_MODEL_LOAD_STAGING_SIZE = 16 * 1024 * 1024; // 16MB
inline constexpr uint32_t TEXTURE_LOAD_STAGING_SIZE    = 16 * 1024 * 1024; // 16MB, Mip0 BC7 == 16777217, which is exactly 16MB

inline constexpr uint32_t DEFAULT_SAMPLER_BINDLESS_INDEX = 0;
inline constexpr uint32_t WHITE_IMAGE_BINDLESS_INDEX = 0;
inline constexpr uint32_t ERROR_IMAGE_BINDLESS_INDEX = 1;
}

#endif //WILL_ENGINE_ASSET_LOAD_CONFIG_H