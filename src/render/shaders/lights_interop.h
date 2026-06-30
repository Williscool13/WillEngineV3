//
// Created by William on 2026-01-12.
//

#ifndef WILL_ENGINE_LIGHTS_INTEROP_H
#define WILL_ENGINE_LIGHTS_INTEROP_H

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

SHADER_PUBLIC SHADER_CONST int MAX_POINT_LIGHTS = 256;
SHADER_PUBLIC SHADER_CONST int MAX_LIGHTS = 2048;

SHADER_PUBLIC SHADER_CONST uint LIGHT_TYPE_AREA = 0u;
SHADER_PUBLIC SHADER_CONST uint LIGHT_TYPE_SPHERE = 1u;

/** Directional light: direction (xyz) + intensity (w), color packed as RGBA8 unorm. */
SHADER_PUBLIC struct DirectionalLightData
{
    SHADER_PUBLIC float4 directionIntensity; // xyz world-space direction, w intensity
    SHADER_PUBLIC uint packedColor; // RGBA8 unorm
    SHADER_PUBLIC float angularRadius; // radians; sun-disk half-angle for soft shadows (0 = hard)
    SHADER_PUBLIC float _pad1;
    SHADER_PUBLIC float _pad2;
};

/** Point light: position (xyz) + range (w), color packed as RGBA8 unorm, intensity separate. */
SHADER_PUBLIC struct PointLightData
{
    SHADER_PUBLIC float4 positionRange; // xyz world-space position, w range
    SHADER_PUBLIC uint packedColor; // RGBA8 unorm
    SHADER_PUBLIC float intensity;
    SHADER_PUBLIC float _pad0;
    SHADER_PUBLIC float _pad1;
};

/** Unified light source tagged by type. Area: rect via normal + right/up half-extents. Sphere: center + radius (right.w); normal/up unused. */
SHADER_PUBLIC struct LightInfo
{
    SHADER_PUBLIC float4 position; // xyz world-space center, w unused
    SHADER_PUBLIC float4 normal; // xyz world-space normal (area only), w unused
    SHADER_PUBLIC float4 right; // xyz right axis (area), w half-width (area) / radius (sphere)
    SHADER_PUBLIC float4 up; // xyz up axis (area only), w half-height (area)
    SHADER_PUBLIC uint packedColor; // RGBA8 unorm
    SHADER_PUBLIC float intensity;
    SHADER_PUBLIC float range; // smoothstep attenuation cutoff distance
    SHADER_PUBLIC uint type; // LIGHT_TYPE_AREA or LIGHT_TYPE_SPHERE
};

/** Light pre-transformed to view space with derived geometry and emission cached. Written once per frame by the ReSTIR transform-lights pass. Sphere: center (xyz) + radius (centerHalfWidth.w), area = 4*pi*r^2 in rightArea.w. */
SHADER_PUBLIC struct LightVSData
{
    SHADER_PUBLIC float4 centerHalfWidth; // xyz view-space center, w half-width (area) / radius (sphere)
    SHADER_PUBLIC float4 normalHalfHeight; // xyz view-space normal (area), w half-height (area)
    SHADER_PUBLIC float4 rightArea; // xyz view-space right axis (area), w area
    SHADER_PUBLIC float4 upRange; // xyz view-space up axis (area), w range
    SHADER_PUBLIC uint packedColor; // RGBA8 unorm
    SHADER_PUBLIC float intensity;
    SHADER_PUBLIC uint type; // LIGHT_TYPE_AREA or LIGHT_TYPE_SPHERE
    SHADER_PUBLIC float _pad1;
};

SHADER_PUBLIC struct LightData
{
    SHADER_PUBLIC int pointLightCount;
    SHADER_PUBLIC int lightCount;
    float _pad0;
    float _pad1;
    SHADER_PUBLIC DirectionalLightData directionalLight;
    SHADER_PUBLIC PointLightData pointLights[MAX_POINT_LIGHTS];
    SHADER_PUBLIC LightInfo lights[MAX_LIGHTS];
};


#endif //WILL_ENGINE_LIGHTS_INTEROP_H
