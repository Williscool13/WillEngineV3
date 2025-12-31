//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_SHADER_INTEROP_H
#define WILLENGINEV3_SHADER_INTEROP_H

#ifdef __SLANG__
module common_interop;
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_ALIGN
#define SHADER_PTR(T) T*
#else
#include <glm/glm.hpp>
#include <cstdint>

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
#endif // __SLANG__


SHADER_PUBLIC struct Frustum
{
    SHADER_PUBLIC float4 planes[6];
};

SHADER_PUBLIC struct SHADER_ALIGN SceneData
{
    SHADER_PUBLIC float4x4 view;
    SHADER_PUBLIC float4x4 proj;
    SHADER_PUBLIC float4x4 viewProj;

    SHADER_PUBLIC float4x4 invView;
    SHADER_PUBLIC float4x4 invProj;
    SHADER_PUBLIC float4x4 invViewProj;
    // SHADER_PUBLIC float4x4 viewProjCameraLookDirection;

    //SHADER_PUBLIC float4x4 prevView;
    //SHADER_PUBLIC float4x4 prevProj;
    SHADER_PUBLIC float4x4 prevViewProj;
    //SHADER_PUBLIC float4x4 prevInvView;
    //SHADER_PUBLIC float4x4 prevInvProj;
    //SHADER_PUBLIC float4x4 prevInvViewProj;
    // SHADER_PUBLIC float4x4 prevViewProjCameraLookDirection;

    SHADER_PUBLIC float4 cameraWorldPos;
    // SHADER_PUBLIC float4 prevCameraWorldPos;
    SHADER_PUBLIC float2 jitter;
    SHADER_PUBLIC float2 prevJitter;

    SHADER_PUBLIC Frustum frustum;

    SHADER_PUBLIC float2 mainRenderTargetSize;
    SHADER_PUBLIC float2 texelSize;

    // SHADER_PUBLIC float2 cameraPlanes;

    SHADER_PUBLIC float deltaTime;
};

#endif // WILLENGINEV3_SHADER_INTEROP_H