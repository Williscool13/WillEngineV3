//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_SHADER_INTEROP_H
#define WILLENGINEV3_SHADER_INTEROP_H

#ifdef __SLANG__
module common_interop;
#define SHADER_PUBLIC public
#define SHADER_ENUM enum
#define SHADER_CONST const static
#define SHADER_ALIGN
#define SHADER_PTR(T) T*
#else
#include <glm/glm.hpp>
#include "core/types/math.h"
#include <cstdint>

using uint = uint32_t;
using int32 = int32_t;
using uint32 = uint32_t;

using float2 = Vec2;
using float3 = Vec3;
using float4 = Vec4;

using int2 = IVec2;
using int3 = IVec3;
using int4 = IVec4;

using uint2 = UVec2;
using uint3 = UVec3;
using uint4 = UVec4;

using float2x2 = Mat2;
using float3x3 = Mat3;
using float4x4 = Mat4;

#define SHADER_PUBLIC
#define SHADER_ENUM enum class
#define SHADER_CONST constexpr inline
#define SHADER_ALIGN alignas(16)
#define SHADER_PTR(T) VkDeviceAddress
#endif // __SLANG__

SHADER_PUBLIC SHADER_ENUM DebugTransformationType
{
    None,
    DepthRemap,
    MultiplyBy1000,
    MultiplyBy10000,
    DivideBy1000,
    DivideBy10000,
    StencilRemap,
    VisBuffTriangle,
    VisBuffMeshlet,
    VisBuffInstance,
    GBufferNormal,
    GBufferMotionVectors,
    GBufferPBR,
    GBufferAlbedo,
    GBufferEmissive,
};

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
    SHADER_PUBLIC float4x4 prevView;
    SHADER_PUBLIC float4x4 prevProj;
    SHADER_PUBLIC float4x4 prevViewProj;
    SHADER_PUBLIC float4x4 unjitteredViewProj;
    SHADER_PUBLIC float4x4 unjitteredPrevViewProj;
    SHADER_PUBLIC float4x4 clipToPrevClip; // currClip -> prevClip space in 1 single multiplication

    SHADER_PUBLIC Frustum frustum;

    SHADER_PUBLIC float4 clipPlane; // near clip plane to define clip distance
    SHADER_PUBLIC float4 cameraWorldPos;

    SHADER_PUBLIC float2 jitter;
    SHADER_PUBLIC float2 prevJitter;

    SHADER_PUBLIC float2 mainRenderTargetSize;
    SHADER_PUBLIC float2 texelSize;

    SHADER_PUBLIC float2 ndcToViewMul;
    SHADER_PUBLIC float2 ndcToViewAdd;

    SHADER_PUBLIC float2 ndcToViewMulXPixelSize;
    SHADER_PUBLIC float2 _pad0;


    SHADER_PUBLIC float depthLinearizeMult;
    SHADER_PUBLIC float depthLinearizeAdd;
    SHADER_PUBLIC float deltaTime;
    SHADER_PUBLIC float lodScreenSizeScale;
};

SHADER_PUBLIC struct ReadbackStruct
{
    SHADER_PUBLIC uint32_t meshletCount;
    SHADER_PUBLIC uint64_t selectedStableId;
};

SHADER_PUBLIC struct DrawMeshTasksIndirectCommand
{
    SHADER_PUBLIC uint32_t groupCountX;
    SHADER_PUBLIC uint32_t groupCountY;
    SHADER_PUBLIC uint32_t groupCountZ;
};

#endif // WILLENGINEV3_SHADER_INTEROP_H