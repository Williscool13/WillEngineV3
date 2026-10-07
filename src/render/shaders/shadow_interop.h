//
// Created by William on 2026-10-07.
//
#ifndef WILL_ENGINE_SHADOW_INTEROP_H
#define WILL_ENGINE_SHADOW_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_PTR(T) T*
#define SHADER_ATOMIC(T) Atomic<T>
import common_interop;
#else
#include <glm/glm.hpp>
#include <cstdint>
#include "common_interop.h"

using uint = uint32_t;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using uint2 = glm::uvec2;

using float4x4 = glm::mat4;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#define SHADER_PTR(T) VkDeviceAddress
#define SHADER_ATOMIC(T) T
#endif // __SLANG__

SHADER_PUBLIC SHADER_CONST uint32_t CSM_MAX_CASCADES = 4;
SHADER_PUBLIC SHADER_CONST uint32_t CSM_ATLAS_COLUMNS = 2;

SHADER_PUBLIC struct CSMCascadeGPU
{
    SHADER_PUBLIC float4x4 viewProj;
    SHADER_PUBLIC Frustum frustum;
    SHADER_PUBLIC float4 atlasScaleOffset;
    SHADER_PUBLIC float halfExtent;
    SHADER_PUBLIC float texelWorldSize;
    SHADER_PUBLIC float depthRange;
    SHADER_PUBLIC float _pad0;
};

SHADER_PUBLIC struct CSMData
{
    SHADER_PUBLIC CSMCascadeGPU cascades[CSM_MAX_CASCADES];
    SHADER_PUBLIC float4 toSun;
    SHADER_PUBLIC uint32_t cascadeCount;
    SHADER_PUBLIC uint32_t resolution;
    SHADER_PUBLIC float normalOffsetTexels;
    SHADER_PUBLIC float _pad0;
};

#endif //WILL_ENGINE_SHADOW_INTEROP_H
