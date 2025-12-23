//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_CONFIG_H
#define WILL_ENGINE_ASSET_LOAD_CONFIG_H
#include <cstdint>

namespace AssetLoad
{
inline constexpr int32_t CONCURRENT_MODEL_LOAD_COUNT = 4;
inline constexpr uint32_t MAX_ASSET_LOAD_JOB_COUNT = 64;
inline constexpr int32_t WILL_MODEL_JOB_COUNT = 4;
inline constexpr int32_t MODEL_LOAD_QUEUE_COUNT = 64;
}

#endif //WILL_ENGINE_ASSET_LOAD_CONFIG_H