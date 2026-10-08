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
import lights_interop;
#else
#include <glm/glm.hpp>
#include <cstdint>
#include "common_interop.h"
#include "lights_interop.h"

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

SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_MAX_VIEWS = 16;
// Area and disk lights coned to at least this cosine get one spot view; wider ones and spheres get cube faces
SHADER_PUBLIC SHADER_CONST float LOCAL_SHADOW_SPOT_COS_MIN = 0.5f;
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_NONE = 0xFFFFFFFF;
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_FACE_SHIFT = 16;
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_STRENGTH_SHIFT = 22;
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_STRENGTH_MAX = 1023;
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_TILE_BITS = 4;
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_TILE_MASK = 0xF;
// lightShadow entry of a light sampled from its baked map: this bit | assetTextureArrays index | strength << LOCAL_SHADOW_STRENGTH_SHIFT
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_BAKED_BIT = 0x8000;
SHADER_PUBLIC SHADER_CONST uint32_t LOCAL_SHADOW_BAKED_INDEX_MASK = 0x7FFF;
// Slightly wider than 90 degrees so PCF taps at a face edge stay inside the tile
SHADER_PUBLIC SHADER_CONST float LOCAL_SHADOW_CUBE_TAN = 1.03f;
SHADER_PUBLIC SHADER_CONST float LOCAL_SHADOW_NEAR = 0.05f;
SHADER_PUBLIC SHADER_CONST float LOCAL_SHADOW_CONE_MARGIN = 0.035f;

// Shadow cull meshletIndexWithinLOD: bits 0..21 meshlet, 22..27 view, 28..31 LOD.
SHADER_PUBLIC SHADER_CONST uint32_t SHADOW_VIEW_SHIFT = 22;
SHADER_PUBLIC SHADER_CONST uint32_t SHADOW_LOD_SHIFT = 28;
SHADER_PUBLIC SHADER_CONST uint32_t SHADOW_MESHLET_MASK = 0x3FFFFF;
SHADER_PUBLIC SHADER_CONST uint32_t SHADOW_VIEW_MASK = 0x3F;

/** Orthographic when eye.w = 0 (eye.xyz = direction to the light), perspective when eye.w = 1 (eye.xyz = light position). */
SHADER_PUBLIC struct ShadowViewGPU
{
    SHADER_PUBLIC float4x4 viewProj;
    SHADER_PUBLIC Frustum frustum;
    SHADER_PUBLIC float4 atlasScaleOffset;
    SHADER_PUBLIC float4 eye;
    // World size of one texel; at unit distance for perspective views
    SHADER_PUBLIC float texelSize;
    SHADER_PUBLIC float halfExtent;
    // World distance between the near and far depth planes
    SHADER_PUBLIC float depthRange;
    SHADER_PUBLIC float nearPlane;
};

SHADER_PUBLIC struct CSMData
{
    SHADER_PUBLIC ShadowViewGPU cascades[CSM_MAX_CASCADES];
    SHADER_PUBLIC float4 toSun;
    SHADER_PUBLIC uint32_t cascadeCount;
    SHADER_PUBLIC uint32_t resolution;
    SHADER_PUBLIC float normalOffsetTexels;
    SHADER_PUBLIC float tanAngularRadius;
    SHADER_PUBLIC float blendBand;
    SHADER_PUBLIC uint32_t bPCSS;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC uint32_t _pad1;
};

// Indexed by atlas tile; tiles stay with their light while it is picked so their depth can be kept between frames
SHADER_PUBLIC struct LocalShadowData
{
    SHADER_PUBLIC ShadowViewGPU views[LOCAL_SHADOW_MAX_VIEWS];
    SHADER_PUBLIC uint2 atlasExtent;
    SHADER_PUBLIC uint32_t lightCount;
    SHADER_PUBLIC float normalOffsetTexels;
    SHADER_PUBLIC uint32_t bPCSS;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC uint32_t _pad1;
    SHADER_PUBLIC uint32_t _pad2;
    // Tile per face, LOCAL_SHADOW_TILE_BITS each, +X -X +Y -Y +Z -Z; a spot uses face 0
    SHADER_PUBLIC uint32_t faceTiles[LOCAL_SHADOW_MAX_VIEWS];
    // Shadowed light record | face mask << LOCAL_SHADOW_FACE_SHIFT (bit 0 alone for a spot) | fade strength << LOCAL_SHADOW_STRENGTH_SHIFT, a baked entry, or LOCAL_SHADOW_NONE
    SHADER_PUBLIC uint32_t lightShadow[MAX_ANALYTIC_LIGHTS];
};

#endif //WILL_ENGINE_SHADOW_INTEROP_H
