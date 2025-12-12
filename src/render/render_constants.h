//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_CONSTANTS_H
#define WILL_ENGINE_VK_CONSTANTS_H
#include <cstdint>

#include "types/render_types.h"

namespace Render
{
inline static constexpr int32_t BINDLESS_MODEL_BUFFER_SIZE              = sizeof(Model) * 16384;
inline static constexpr int32_t BINDLESS_INSTANCE_BUFFER_SIZE           = sizeof(Instance) * 131072;
inline static constexpr int32_t BINDLESS_COMBINED_IMAGE_SAMPLER_COUNT   = 512;
inline static constexpr int32_t BINDLESS_STORAGE_IMAGE_COUNT            = 128;
inline static constexpr int32_t BINDLESS_SAMPLER_COUNT                  = 64;
inline static constexpr int32_t BINDLESS_SAMPLED_IMAGE_COUNT            = 1024;
} // Render

#endif //WILL_ENGINE_VK_CONSTANTS_H