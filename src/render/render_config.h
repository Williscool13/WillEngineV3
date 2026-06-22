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

namespace Render
{
inline constexpr int32_t RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD = 1024; // in ticks
inline constexpr int32_t RDG_MAX_TEXTURES = 2048;
inline constexpr int32_t RDG_MAX_BUFFERS = 2048;

inline constexpr int32_t RDG_MAX_SAMPLED_TEXTURES = 256;
inline constexpr int32_t RDG_MAX_SAMPLED_FLOAT2 = 128;
inline constexpr int32_t RDG_MAX_SAMPLED_FLOAT = 256;
inline constexpr int32_t RDG_MAX_SAMPLED_UINT4 = 64;
inline constexpr int32_t RDG_MAX_SAMPLED_UINT2 = 64;
inline constexpr int32_t RDG_MAX_SAMPLED_UINT = 64;
inline constexpr int32_t RDG_MAX_MULTISAMPLED_IMAGE = 4;
inline constexpr int32_t RDG_MAX_MULTISAMPLED_UINT_IMAGE = 4;
inline constexpr int32_t RDG_MAX_STORAGE_FLOAT4 = 512;
inline constexpr int32_t RDG_MAX_STORAGE_FLOAT2 = 128;
inline constexpr int32_t RDG_MAX_STORAGE_FLOAT = 256;
inline constexpr int32_t RDG_MAX_STORAGE_UINT4 = 64;
inline constexpr int32_t RDG_MAX_STORAGE_UINT2 = 64;
inline constexpr int32_t RDG_MAX_STORAGE_UINT = 64;
inline constexpr int32_t RDG_MAX_TLAS = 8;

inline const StringID SCENE_DATA_BUFFER = SID("scene_data");
inline const StringID LIGHT_DATA_BUFFER = SID("light_data");
inline const StringID GEOMETRY_PRIMITIVE_BUFFER = SID("primitive_buffer");
inline const StringID GEOMETRY_MODEL_BUFFER = SID("model_buffer");
inline const StringID GEOMETRY_INSTANCE_BUFFER = SID("main_instance_buffer");
inline const StringID GEOMETRY_VERTEX_POSITION_BUFFER = SID("vertex_position_buffer");
inline const StringID GEOMETRY_VERTEX_ATTRIBUTE_BUFFER = SID("vertex_attribute_buffer");
inline const StringID GEOMETRY_MESHLET_VERTEX_BUFFER = SID("meshlet_vertex_buffer");
inline const StringID GEOMETRY_MESHLET_TRIANGLE_BUFFER = SID("meshlet_triangle_buffer");
inline const StringID GEOMETRY_MESHLET_BUFFER = SID("meshlet_buffer");
inline const StringID GEOMETRY_MATERIAL_BUFFER = SID("material_buffer");

inline const StringID SPRITE_BUFFER = SID("sprite_buffer");

inline const StringID TEXT_GLYPH_QUAD_BUFFER = SID("text_glyph_quad_buffer");
inline const StringID UI_GLYPH_QUAD_BUFFER = SID("ui_glyph_quad_buffer");
inline const StringID TEXT_INSTANCE_BUFFER = SID("text_instance_buffer");
inline const StringID TEXT_MATERIAL_BUFFER = SID("text_material_buffer");

inline const StringID SHADING_DISPATCH_BUCKETING_BUFFER = SID("shading_bucketing_buffer");
inline const StringID LIGHTING_DISPATCH_BUCKETING_BUFFER = SID("lighting_bucketing_buffer");

inline const StringID RT_TLAS_INSTANCE_BUFFER = SID("rt_tlas_instance_buffer");
inline const StringID RT_TLAS_BUFFER = SID("rt_tlas_buffer");
inline const StringID RT_TLAS_SCRATCH_BUFFER = SID("rt_tlas_scratch_buffer");

inline constexpr int32_t RDG_MAX_MIP_LEVELS = 12;
inline constexpr int32_t RDG_MAX_PASSES = 256;
inline constexpr int32_t RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE = 1 * 1024 * 1024; // 1MB base (doubles as needed)

inline constexpr int32_t BINDLESS_MODEL_BUFFER_COUNT = 16384;
inline constexpr int32_t BINDLESS_MODEL_BUFFER_SIZE = sizeof(Model) * BINDLESS_MODEL_BUFFER_COUNT;
inline constexpr int32_t BINDLESS_INSTANCE_BUFFER_COUNT = 131072;
inline constexpr int32_t BINDLESS_INSTANCE_BUFFER_SIZE = sizeof(Instance) * BINDLESS_INSTANCE_BUFFER_COUNT;
inline constexpr int32_t BINDLESS_MATERIAL_BUFFER_COUNT = 2048;
inline constexpr int32_t BINDLESS_MATERIAL_BUFFER_SIZE = sizeof(MaterialProperties) * BINDLESS_MATERIAL_BUFFER_COUNT;

inline constexpr int32_t MEGA_VERTEX_POSITION_BUFFER_SIZE = sizeof(VertexPosition) * 4194302; // 4M verts
inline constexpr int32_t MEGA_VERTEX_ATTRIBUTE_BUFFER_SIZE = sizeof(VertexAttribute) * 4194302; // 4M verts
inline constexpr int32_t MEGA_INDEX_BUFFER_SIZE = sizeof(uint32_t) * 16777216; // 16M indices (64MB)
inline constexpr int32_t MEGA_BLAS_BUFFER_SIZE = 1 << 28; // 256MB for BLAS storage
inline constexpr int32_t MEGA_PRIMITIVE_BUFFER_COUNT = 65536; // Important for instancing - it's 256x256
inline constexpr int32_t MEGA_PRIMITIVE_BUFFER_SIZE = sizeof(Primitive) * MEGA_PRIMITIVE_BUFFER_COUNT;

inline constexpr int32_t VIEW_COUNT = 4; // Up to 4 views per frame, 0 is main view. 1 is portal. Idk what 2/3 are (maybe remove)
inline constexpr int32_t SCENE_DATA_BUFFER_SIZE = sizeof(SceneData) * VIEW_COUNT;
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

// Halton(2,3) radical inverses for i = 1..16, minus the window means (0.470703125, 0.462962963) so the sequence sums to exactly zero and carries no DC image shift.
// Units are pixels; one pixel is 2/extent in NDC, so the projection offset is sample * 2 / renderExtent.
inline constexpr Core::Array<HaltonSample, HALTON_SEQUENCE_COUNT> HALTON_SEQUENCE{
    HaltonSample{0.02929688f, -0.12962963f},
    {-0.22070312f, 0.20370370f},
    {0.27929688f, -0.35185185f},
    {-0.34570312f, -0.01851852f},
    {0.15429688f, 0.31481481f},
    {-0.09570312f, -0.24074074f},
    {0.40429688f, 0.09259259f},
    {-0.40820312f, 0.42592593f},
    {0.09179688f, -0.42592593f},
    {-0.15820312f, -0.09259259f},
    {0.34179688f, 0.24074074f},
    {-0.28320312f, -0.31481481f},
    {0.21679688f, 0.01851852f},
    {-0.03320312f, 0.35185185f},
    {0.46679688f, -0.20370370f},
    {-0.43945312f, 0.12962963f},
};


} // Render

#endif //WILL_ENGINE_VK_CONSTANTS_H
