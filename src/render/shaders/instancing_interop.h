//
// Created by William on 2025-12-29.
//

#ifndef WILL_ENGINE_INSTANCING_INTEROP_H
#define WILL_ENGINE_INSTANCING_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_PTR(T) T*
#define SHADER_ATOMIC(T) Atomic<T>
import common_interop;
import model_interop;
import constants_interop;
#else
#include <glm/glm.hpp>
#include <cstdint>
#include "common_interop.h"
#include "model_interop.h"

using uint = uint32_t;
using int32 = int32_t;
using uint32 = uint32_t;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using int2 = glm::ivec2;
using int3 = glm::ivec3;
using int4 = glm::ivec4;

using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;

using float2x2 = glm::mat2;
using float3x3 = glm::mat3;
using float4x4 = glm::mat4;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#define SHADER_PTR(T) VkDeviceAddress
#define SHADER_ATOMIC(T) T
#endif // __SLANG__

SHADER_PUBLIC struct InstanceMeshletOffsetPrefixSum
{
    SHADER_PUBLIC uint32_t offset;
    SHADER_PUBLIC uint32_t count;
    SHADER_PUBLIC uint32_t lod;
    SHADER_PUBLIC uint32_t primitiveIndex; // debug only
};

SHADER_PUBLIC struct InstancingMeshletDispatchIndirect
{
    SHADER_PUBLIC uint32_t x;
    SHADER_PUBLIC uint32_t y;
    SHADER_PUBLIC uint32_t z;
    SHADER_PUBLIC uint32_t totalMeshlets;
};

// Visible meshlets are partitioned into draw regions by material class x facing; index = (cutout ? 2 : 0) | (twoSided ? 1 : 0)
SHADER_PUBLIC SHADER_CONST uint32_t MESHLET_REGION_OPAQUE = 0;
SHADER_PUBLIC SHADER_CONST uint32_t MESHLET_REGION_OPAQUE_TWO_SIDED = 1;
SHADER_PUBLIC SHADER_CONST uint32_t MESHLET_REGION_CUTOUT = 2;
SHADER_PUBLIC SHADER_CONST uint32_t MESHLET_REGION_CUTOUT_TWO_SIDED = 3;
SHADER_PUBLIC SHADER_CONST uint32_t MESHLET_REGION_COUNT = 4;

SHADER_PUBLIC struct IntermediateMeshlet
{
    SHADER_PUBLIC uint32_t instanceIndex; // bit 31 visible, bits 29..30 draw region, bits 0..28 instance index
    SHADER_PUBLIC uint32_t meshletIndexWithinLOD; // 2/30, greatest 2 bits are LOD
};

SHADER_PUBLIC struct CompactedMeshlet
{
    SHADER_PUBLIC uint32_t instanceIndex; // 32 for instanceIndex
    SHADER_PUBLIC uint32_t meshletIndexWithinLOD; // 2/30, greatest 2 bits are LOD
};

SHADER_PUBLIC struct InstancingCompactedMeshletDispatchIndirect
{
    // Per region: xyz = mesh task dispatch, w = meshlet count
    SHADER_PUBLIC uint4 regionArgs[MESHLET_REGION_COUNT];
    // Start of each region in the visible meshlet buffer
    SHADER_PUBLIC uint4 regionBase;
    SHADER_PUBLIC uint32_t totalVisibleMeshlets;
};

SHADER_PUBLIC struct PrimitiveCounters
{
    SHADER_PUBLIC uint32_t visibleCountPerLOD[LOD_COUNT];
    SHADER_PUBLIC uint32_t lodInstanceOffset[LOD_COUNT]; // After final instancing setup pass, should be equal to visibleCountPerLOD
};

SHADER_PUBLIC struct PrimitiveOffsets
{
    SHADER_PUBLIC uint32_t instanceOffset;
    SHADER_PUBLIC uint32_t commandOffset;
};

SHADER_PUBLIC struct InstancedMeshIndirectCountBuffer
{
    SHADER_PUBLIC uint32_t indirectCount;
    SHADER_PUBLIC uint32_t totalInstanceCount;
    uint32_t padding1;
    uint32_t padding2;
};

#endif //WILL_ENGINE_INSTANCING_INTEROP_H
