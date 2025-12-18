//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_CONFIG_H
#define WILL_ENGINE_ASSET_LOAD_CONFIG_H
#include <cstdint>

namespace AssetLoad
{
inline constexpr int32_t ASSET_LOAD_ASYNC_COUNT = 4;
inline static constexpr int32_t ASSET_LOAD_QUEUE_COUNT = 64;
inline static constexpr uint32_t MAX_LOADED_MODELS = 1024;
}

#endif //WILL_ENGINE_ASSET_LOAD_CONFIG_H