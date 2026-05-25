//
// Created by William on 2026-05-25.
//

#ifndef WILL_ENGINE_RETIR_INTEROP_H
#define WILL_ENGINE_RETIR_INTEROP_H

#ifdef __SLANG__
module restir_interop;
#define SHADER_PUBLIC public
#else
#include <glm/glm.hpp>
#include <cstdint>

using uint = uint32_t;
using float2 = glm::vec2;
#define SHADER_PUBLIC
#endif // __SLANG__

/**
 * Per-pixel reservoir written by the ReSTIR DI generate pass and read by the shade pass.
 * 16 bytes, naturally aligned.
 *
 * sampleOffset is the (u, v) offset from the light center along its right/up axes (view space, world units).
 * Reconstruct samplePos = lightCenter + sampleOffset.x * lightRight + sampleOffset.y * lightUp.
 * lightIdx == ~0u indicates an empty reservoir.
 * wSum and M are transient locals during generation and are not stored.
 */
SHADER_PUBLIC struct Reservoir
{
    SHADER_PUBLIC float2 sampleOffset;
    SHADER_PUBLIC uint lightIdx;
    SHADER_PUBLIC float W;
};

#endif //WILL_ENGINE_RETIR_INTEROP_H
