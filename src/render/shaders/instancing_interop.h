//
// Created by William on 2025-12-29.
//

#ifndef WILL_ENGINE_INSTANCING_INTEROP_H
#define WILL_ENGINE_INSTANCING_INTEROP_H

#ifdef __SLANG__
module instancing_interop;
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_ALIGN
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
#define SHADER_ALIGN alignas(16)
#define SHADER_PTR(T) VkDeviceAddress
#define SHADER_ATOMIC(T) T
#endif // __SLANG__

SHADER_PUBLIC struct InstancedMeshIndirectDrawParameters
{
    // indirect parameters
    SHADER_PUBLIC uint32_t groupCountX; // meshlet count / group size
    SHADER_PUBLIC uint32_t groupCountY; // instance offset
    SHADER_PUBLIC uint32_t groupCountZ;

    SHADER_PUBLIC uint32_t compactedInstanceStart;
    SHADER_PUBLIC uint32_t compactedInstanceCount;

    SHADER_PUBLIC uint32_t meshletOffset;
    SHADER_PUBLIC uint32_t meshletCount;
    uint32_t padding0;
};

// Direct to mesh shader, no task shader
SHADER_PUBLIC struct CustomMeshRenderingDrawParameters
{
    SHADER_PUBLIC uint32_t groupCountX; // meshlet count / group size
    SHADER_PUBLIC uint32_t groupCountY; // always 1
    SHADER_PUBLIC uint32_t groupCountZ; // always 1

    SHADER_PUBLIC uint32_t compactedInstanceStart;
    SHADER_PUBLIC uint32_t compactedInstanceCount;

    SHADER_PUBLIC uint32_t meshletOffset;
    SHADER_PUBLIC uint32_t meshletCount;
    uint32_t padding0;
};


SHADER_PUBLIC struct InstanceMeshletOffsetPrefixSum
{
    SHADER_PUBLIC uint32_t offset;
    SHADER_PUBLIC uint32_t count;
    SHADER_PUBLIC uint32_t lod;
    SHADER_PUBLIC uint32_t primitiveIndex; // debug only
};

SHADER_PUBLIC struct InstancingMeshletDispatchIndirectCommand
{
    SHADER_PUBLIC uint32_t x;
    SHADER_PUBLIC uint32_t y;
    SHADER_PUBLIC uint32_t z;
    SHADER_PUBLIC uint32_t totalMeshlets;
};

SHADER_PUBLIC struct IntermediateMeshlet
{
    SHADER_PUBLIC uint32_t instanceIndex;
    SHADER_PUBLIC uint32_t localMeshletIndex; // meshlet index within the primitive
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
