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
inline constexpr int32_t BINDLESS_MODEL_BUFFER_COUNT             = 16384;
inline constexpr int32_t BINDLESS_MODEL_BUFFER_SIZE              = sizeof(Model) * BINDLESS_MODEL_BUFFER_COUNT;
inline constexpr int32_t BINDLESS_INSTANCE_BUFFER_COUNT          = 131072;
inline constexpr int32_t BINDLESS_INSTANCE_BUFFER_SIZE           = sizeof(Instance) * BINDLESS_INSTANCE_BUFFER_COUNT;
inline constexpr int32_t BINDLESS_MATERIAL_BUFFER_COUNT          = 2048;
inline constexpr int32_t BINDLESS_MATERIAL_BUFFER_SIZE           = sizeof(MaterialProperties) * BINDLESS_MATERIAL_BUFFER_COUNT;

inline constexpr int32_t BINDLESS_COMBINED_IMAGE_SAMPLER_COUNT   = 0;
inline constexpr int32_t BINDLESS_STORAGE_IMAGE_COUNT            = 128;
inline constexpr int32_t BINDLESS_SAMPLER_COUNT                  = 128;
inline constexpr int32_t BINDLESS_SAMPLED_IMAGE_COUNT            = 4096;

inline constexpr int32_t MEGA_VERTEX_BUFFER_SIZE                 = sizeof(Vertex) * 2097152;             // 2M verts (~100MB)
inline constexpr int32_t MEGA_SKINNED_VERTEX_BUFFER_SIZE         = sizeof(SkinnedVertex) * 1048576;      // 1M skinned (~100MB)
inline constexpr int32_t MEGA_PRIMITIVE_BUFFER_SIZE              = sizeof(MeshletPrimitive) * 131072;    // 128K primitives

inline constexpr int32_t MEGA_MESHLET_VERTEX_BUFFER_SIZE         = 1 << 26; // 64MB indices
inline constexpr int32_t MEGA_MESHLET_TRIANGLE_BUFFER_SIZE       = 1 << 26; // 64MB triangles
inline constexpr int32_t MEGA_MESHLET_BUFFER_SIZE                = 1 << 21; // 2MB meshlets

inline constexpr uint32_t FRAME_BUFFER_OPERATION_COUNT_LIMIT = 1024;
} // Render

#endif //WILL_ENGINE_VK_CONSTANTS_H