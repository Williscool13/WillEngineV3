//
// Created by William on 2026-01-12.
//

#ifndef WILL_ENGINE_LIGHTS_INTEROP_H
#define WILL_ENGINE_LIGHTS_INTEROP_H

#ifdef __SLANG__
module SHADOWS_interop;
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
#include "constants_interop.h"

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

SHADER_CONST int MAX_POINT_LIGHTS = 256;
SHADER_CONST int MAX_AREA_LIGHTS = 256;

/** Directional light: direction (xyz) + intensity (w), color packed as RGBA8 unorm. */
SHADER_PUBLIC struct DirectionalLightData
{
    SHADER_PUBLIC float4 directionIntensity; // xyz world-space direction, w intensity
    SHADER_PUBLIC uint packedColor;          // RGBA8 unorm
    SHADER_PUBLIC float _pad0;
    SHADER_PUBLIC float _pad1;
    SHADER_PUBLIC float _pad2;
};

/** Point light: position (xyz) + range (w), color packed as RGBA8 unorm, intensity separate. */
SHADER_PUBLIC struct PointLightData
{
    SHADER_PUBLIC float4 positionRange; // xyz world-space position, w range
    SHADER_PUBLIC uint packedColor;     // RGBA8 unorm
    SHADER_PUBLIC float intensity;
    SHADER_PUBLIC float _pad0;
    SHADER_PUBLIC float _pad1;
};

/** Rectangular area light: position, normal, right/up half-extents, color. */
SHADER_PUBLIC struct AreaLightData
{
    SHADER_PUBLIC float4 position;      // xyz world-space center, w unused
    SHADER_PUBLIC float4 normal;        // xyz world-space normal, w unused
    SHADER_PUBLIC float4 right;         // xyz right axis, w half-width
    SHADER_PUBLIC float4 up;            // xyz up axis, w half-height
    SHADER_PUBLIC uint packedColor;     // RGBA8 unorm
    SHADER_PUBLIC float intensity;
    SHADER_PUBLIC float range;          // smoothstep attenuation cutoff distance
    SHADER_PUBLIC float _pad1;
};

SHADER_PUBLIC struct LightData
{
    SHADER_PUBLIC int pointLightCount;
    SHADER_PUBLIC int areaLightCount;
    float _pad0;
    float _pad1;
    SHADER_PUBLIC DirectionalLightData directionalLight;
    SHADER_PUBLIC PointLightData pointLights[MAX_POINT_LIGHTS];
    SHADER_PUBLIC AreaLightData areaLights[MAX_AREA_LIGHTS];
};


#endif //WILL_ENGINE_LIGHTS_INTEROP_H