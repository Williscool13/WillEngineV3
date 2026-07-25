//
// Created by William on 2026-07-09.
//

#ifndef WILL_ENGINE_REFLECTION_INTEROP_H
#define WILL_ENGINE_REFLECTION_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST static const
#else
#include <cstdint>
#include <glm/glm.hpp>
using uint = uint32_t;
using float2 = glm::vec2;
using float3 = glm::vec3;
#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#endif // __SLANG__

/** hitT >= 0: valid surface hit (instanceID/primIndex/bary). REFLECTION_HIT_T_SKY: escaped to sky, sample rayDir. REFLECTION_HIT_T_OWNED: already shaded by ReSTIR's own specular term, contribute nothing. */
SHADER_PUBLIC struct ReflectionHitDescriptor
{
    SHADER_PUBLIC float3 rayDir;
    SHADER_PUBLIC float hitT;
    SHADER_PUBLIC float2 bary;
    SHADER_PUBLIC uint instanceID;
    SHADER_PUBLIC uint primIndex;
};

SHADER_PUBLIC SHADER_CONST float REFLECTION_HIT_T_SKY = -1.0;
SHADER_PUBLIC SHADER_CONST float REFLECTION_HIT_T_OWNED = -2.0;

SHADER_PUBLIC SHADER_CONST uint REFLECTION_INSTANCE_NONE = 0xFFFFFFFFu;

#endif //WILL_ENGINE_REFLECTION_INTEROP_H
