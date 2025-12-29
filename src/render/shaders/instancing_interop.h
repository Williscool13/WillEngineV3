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

SHADER_PUBLIC struct PrimitiveCount
{
    SHADER_PUBLIC SHADER_ATOMIC(uint32_t) count;
    SHADER_PUBLIC uint32_t offset;
};

SHADER_PUBLIC struct InstancedMeshIndirectDrawParameters
{
    // indirect parameters
    SHADER_PUBLIC uint32_t groupCountX;
    SHADER_PUBLIC uint32_t groupCountY; // offset
    SHADER_PUBLIC uint32_t groupCountZ;
    SHADER_PUBLIC uint32_t compactedInstanceStart;

    SHADER_PUBLIC uint32_t meshletOffset;
    SHADER_PUBLIC uint32_t meshletCount;
    SHADER_PUBLIC uint32_t padding1;
    SHADER_PUBLIC uint32_t padding2;
};

SHADER_PUBLIC struct InstancedMeshIndirectCountBuffer
{
    SHADER_PUBLIC SHADER_ATOMIC(uint32_t) meshIndirectCount;
    SHADER_PUBLIC uint32_t padding0;
    SHADER_PUBLIC uint32_t padding1;
    SHADER_PUBLIC uint32_t padding2;
};

#endif //WILL_ENGINE_INSTANCING_INTEROP_H
