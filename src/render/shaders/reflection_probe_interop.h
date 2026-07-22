//
// Created by William on 2026-07-22.
//

#ifndef WILL_ENGINE_REFLECTION_PROBE_INTEROP_H
#define WILL_ENGINE_REFLECTION_PROBE_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_PTR(T) T*
import common_interop;
#else
#include <glm/glm.hpp>
#include <cstdint>
#include "common_interop.h"

using uint = uint32_t;

using float3 = glm::vec3;
using float4 = glm::vec4;
using float4x4 = glm::mat4;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#define SHADER_PTR(T) VkDeviceAddress
#endif // __SLANG__

SHADER_PUBLIC SHADER_CONST int MAX_REFLECTION_PROBES = 64;

SHADER_PUBLIC SHADER_CONST uint REFLECTION_PROBE_FLAG_SPHERE = 1u;
SHADER_PUBLIC SHADER_CONST uint REFLECTION_PROBE_FLAG_PARALLAX = 2u;

/** Per-frame reflection probe. worldToLocal maps a world point into the [-1,1] influence volume (box OBB, or sphere via uniform scale); capturePosition.xyz is the world-space capture origin for parallax correction. */
SHADER_PUBLIC struct ReflectionProbeGPU
{
    SHADER_PUBLIC float4x4 worldToLocal;
    SHADER_PUBLIC float4 capturePosition; // xyz world-space capture origin, w unused
    SHADER_PUBLIC uint cubemapIndex; // bindless cubemap index
    SHADER_PUBLIC float fadeMargin;
    SHADER_PUBLIC uint flags; // REFLECTION_PROBE_FLAG_*
    SHADER_PUBLIC uint _pad0;
};

#endif //WILL_ENGINE_REFLECTION_PROBE_INTEROP_H
