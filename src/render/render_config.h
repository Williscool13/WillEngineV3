//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_CONSTANTS_H
#define WILL_ENGINE_VK_CONSTANTS_H
#include <cstdint>

#include "core/string_id.h"
#include "core/containers/array.h"
#include "shaders/lights_interop.h"
#include "shaders/model_interop.h"
#include "shaders/shadows_interop.h"

namespace Render
{
inline constexpr int32_t RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD = 1024; // in ticks
inline constexpr int32_t RDG_MAX_SAMPLED_TEXTURES = 256;
inline constexpr int32_t RDG_MAX_STORAGE_FLOAT4 = 512;
inline constexpr int32_t RDG_MAX_STORAGE_FLOAT2 = 128;
inline constexpr int32_t RDG_MAX_STORAGE_FLOAT  = 256;
inline constexpr int32_t RDG_MAX_STORAGE_UINT4   = 64;
inline constexpr int32_t RDG_MAX_STORAGE_UINT2   = 64;
inline constexpr int32_t RDG_MAX_STORAGE_UINT   = 64;

inline const StringID SCENE_DATA_BUFFER = SID("scene_data");
inline const StringID SHADOW_DATA_BUFFER = SID("shadow_data");
inline const StringID GEOMETRY_PRIMITIVE_BUFFER = SID("primitive_buffer");
inline const StringID GEOMETRY_MODEL_BUFFER = SID("model_buffer");
inline const StringID GEOMETRY_INSTANCE_BUFFER = SID("main_instance_buffer");
inline const StringID GEOMETRY_VERTEX_BUFFER = SID("vertex_buffer");
inline const StringID GEOMETRY_MESHLET_VERTEX_BUFFER = SID("meshlet_vertex_buffer");
inline const StringID GEOMETRY_MESHLET_TRIANGLE_BUFFER = SID("meshlet_triangle_buffer");
inline const StringID GEOMETRY_MESHLET_BUFFER = SID("meshlet_buffer");
inline const StringID GEOMETRY_MATERIAL_BUFFER = SID("material_buffer");

inline constexpr int32_t RDG_MAX_MIP_LEVELS = 12;
inline constexpr int32_t RDG_MAX_PASSES = 256;
inline constexpr int32_t RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE = 1 * 1024 * 1024; // 1MB base (doubles as needed)

inline constexpr int32_t BINDLESS_MODEL_BUFFER_COUNT = 16384;
inline constexpr int32_t BINDLESS_MODEL_BUFFER_SIZE = sizeof(Model) * BINDLESS_MODEL_BUFFER_COUNT;
inline constexpr int32_t BINDLESS_INSTANCE_BUFFER_COUNT = 131072;
inline constexpr int32_t BINDLESS_INSTANCE_BUFFER_SIZE = sizeof(Instance) * BINDLESS_INSTANCE_BUFFER_COUNT;
inline constexpr int32_t BINDLESS_MATERIAL_BUFFER_COUNT = 2048;
inline constexpr int32_t BINDLESS_MATERIAL_BUFFER_SIZE = sizeof(MaterialProperties) * BINDLESS_MATERIAL_BUFFER_COUNT;

inline constexpr int32_t MEGA_VERTEX_BUFFER_SIZE = sizeof(Vertex) * 4194302; // 4M verts
inline constexpr int32_t MEGA_PRIMITIVE_BUFFER_COUNT = 65536; // Important for instancing - it's 256x256
inline constexpr int32_t MEGA_PRIMITIVE_BUFFER_SIZE = sizeof(Primitive) * MEGA_PRIMITIVE_BUFFER_COUNT;

inline constexpr int32_t VIEW_COUNT = 4; // Up to 4 views per frame, 0 is main view. 1 is portal. Idk what 2/3 are (maybe remove)
inline constexpr int32_t SCENE_DATA_BUFFER_SIZE = sizeof(SceneData) * VIEW_COUNT;
inline constexpr int32_t SHADOW_DATA_BUFFER_SIZE = sizeof(ShadowData) * SHADOW_CASCADE_COUNT * VIEW_COUNT;
inline constexpr int32_t LIGHT_DATA_BUFFER_SIZE = sizeof(LightData) * VIEW_COUNT;

inline constexpr int32_t MEGA_MESHLET_VERTEX_BUFFER_SIZE = 1 << 27; // 64MB indices
inline constexpr int32_t MEGA_MESHLET_TRIANGLE_BUFFER_SIZE = 1 << 27; // 64MB triangles
inline constexpr int32_t MEGA_MESHLET_BUFFER_SIZE = 1 << 24; // 2MB meshlets

inline constexpr int32_t BINDLESS_COMBINED_IMAGE_SAMPLER_COUNT = 1;
inline constexpr int32_t BINDLESS_STORAGE_IMAGE_COUNT = 128;
inline constexpr int32_t BINDLESS_SAMPLER_COUNT = 128;
inline constexpr int32_t BINDLESS_SAMPLED_IMAGE_COUNT = 4096;
inline constexpr int32_t BINDLESS_SAMPLED_CUBEMAP_COUNT = 128;

// inline constexpr DXGI_FORMAT ENVIRONMENT_MAP_TARGET_FORMAT = DXGI_FORMAT_BC6H_UF16;


inline constexpr int32_t POST_PROCESS_LUMINANCE_BUFFER_SIZE = sizeof(uint32_t) * POST_PROCESS_LUMINANCE_DISPATCH_X * POST_PROCESS_LUMINANCE_DISPATCH_Y;

inline constexpr uint32_t HALTON_SEQUENCE_COUNT = 16;

inline constexpr uint32_t LOD_BIAS = 0;

struct HaltonSample
{
    float x, y;
};

// Pre-computed 16-point Halton sequence (base 2, 3)
inline constexpr Core::Array<HaltonSample, HALTON_SEQUENCE_COUNT> HALTON_SEQUENCE{
    HaltonSample(0.5f, 0.5f),
    {0.25f, 0.66666667f},
    {0.75f, 0.11111111f},
    {0.125f, 0.44444445f},
    {0.625f, 0.7777778f},
    {0.375f, 0.22222222f},
    {0.875f, 0.5555556f},
    {0.0625f, 0.8888889f},
    {0.5625f, 0.037037037f},
    {0.3125f, 0.3703704f},
    {0.8125f, 0.7037037f},
    {0.1875f, 0.14814815f},
    {0.6875f, 0.4814815f},
    {0.4375f, 0.8148148f},
    {0.9375f, 0.25925925f},
    {0.03125f, 0.5925926f},
};



struct CascadeBias
{
    float linear;
    float sloped;
};

struct PCSSSamples
{
    uint32_t blockerSearchSamples;
    uint32_t pcfSamples;
};

struct ShadowCascadePreset
{
    Core::Array<glm::vec2, SHADOW_CASCADE_COUNT> extents;
    Core::Array<CascadeBias, SHADOW_CASCADE_COUNT> biases;
    Core::Array<PCSSSamples, SHADOW_CASCADE_COUNT> pcssSamples;
    Core::Array<float, SHADOW_CASCADE_COUNT> lightSizes;
};

inline constexpr Core::Array<ShadowCascadePreset, 4> SHADOW_PRESETS = {
    {
        // Ultra
        {
            {
                glm::vec2{4096, 4096}, {2048, 2048}, {1024, 1024}, {1024, 1024}
            },
            {
                CascadeBias{0, 7.0f}, {0, 3.0f}, {0, 2.0f}, {0, 1.5f}
            },
            {
                PCSSSamples{32, 64}, {32, 64}, {16, 32}, {16, 32}
            },
            {
                0.003f, 0.003f, 0.003f, 0.002f
            }
        },

        // High
        {
            {
                glm::vec2{2048, 2048}, {2048, 2048}, {1024, 1024}, {512, 512}
            },
            {
                CascadeBias{1.5f, 2.0f}, {1.75f, 2.25f}, {2.25f, 2.75f}, {3.0f, 3.5f}
            },
            {
                PCSSSamples{24, 48}, {24, 48}, {16, 32}, {12, 24}
            },
            {
                0.006f, 0.012f, 0.024f, 0.048f
            }
        },

        // Medium
        {
            {
                glm::vec2{2048, 2048}, {1024, 1024}, {512, 512}, {512, 512}
            },
            {
                CascadeBias{2.0f, 2.5f}, {2.5f, 3.0f}, {3.0f, 3.5f}, {4.0f, 4.5f}
            },
            {
                PCSSSamples{16, 32}, {16, 32}, {12, 24}, {8, 16}
            },
            {
                0.008f, 0.016f, 0.032f, 0.064f
            }
        },

        // Low
        {
            {
                glm::vec2{1024, 1024}, {1024, 1024}, {512, 512}, {256, 256}
            },
            {
                CascadeBias{2.5f, 3.0f}, {3.0f, 3.5f}, {4.0f, 4.5f}, {5.0f, 5.5f}
            },
            {
                PCSSSamples{12, 24}, {12, 24}, {8, 16}, {8, 16}
            },
            {
                0.01f, 0.02f, 0.04f, 0.08f
            }
        }
    }
};
} // Render

#endif //WILL_ENGINE_VK_CONSTANTS_H
