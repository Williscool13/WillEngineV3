//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_CONSTANTS_H
#define WILL_ENGINE_VK_CONSTANTS_H
#include <cstdint>

#include "shaders/model_interop.h"
#include "types/render_types.h"
#include "types/vk_types.h"

namespace Render
{
inline static constexpr int32_t BINDLESS_MODEL_BUFFER_COUNT             = 16384;
inline static constexpr int32_t BINDLESS_MODEL_BUFFER_SIZE              = sizeof(Model) * BINDLESS_MODEL_BUFFER_COUNT;
inline static constexpr int32_t BINDLESS_INSTANCE_BUFFER_COUNT          = 131072;
inline static constexpr int32_t BINDLESS_INSTANCE_BUFFER_SIZE           = sizeof(Instance) * BINDLESS_INSTANCE_BUFFER_COUNT;
inline static constexpr int32_t BINDLESS_MATERIAL_BUFFER_COUNT          = 512;
inline static constexpr int32_t BINDLESS_MATERIAL_BUFFER_SIZE           = sizeof(MaterialProperties) * BINDLESS_MATERIAL_BUFFER_COUNT;

inline static constexpr int32_t BINDLESS_COMBINED_IMAGE_SAMPLER_COUNT   = 512;
inline static constexpr int32_t BINDLESS_STORAGE_IMAGE_COUNT            = 128;
inline static constexpr int32_t BINDLESS_SAMPLER_COUNT                  = 64;
inline static constexpr int32_t BINDLESS_SAMPLED_IMAGE_COUNT            = 1024;

inline static constexpr int32_t MEGA_VERTEX_BUFFER_SIZE                 = sizeof(SkinnedVertex) * 1048576; // 1 << 20
inline static constexpr int32_t MEGA_PRIMITIVE_BUFFER_SIZE              = sizeof(MeshletPrimitive) * 65536;

inline static constexpr int32_t MEGA_MESHLET_VERTEX_BUFFER_SIZE         = 1 << 25; // sizeof(uint32_t)
inline static constexpr int32_t MEGA_MESHLET_TRIANGLE_BUFFER_SIZE       = 1 << 25; // sizeof(uint8_t)
inline static constexpr int32_t MEGA_MESHLET_BUFFER_SIZE                = 1 << 20; // sizeof(Meshlet)
inline static constexpr uint32_t MAX_LOADED_MODELS                      = 1024;
} // Render

#endif //WILL_ENGINE_VK_CONSTANTS_H