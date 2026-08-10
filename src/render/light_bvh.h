//
// Created by William on 2026-06-22.
//

#ifndef WILL_ENGINE_LIGHT_BVH_H
#define WILL_ENGINE_LIGHT_BVH_H

#include <cstdint>

#include "core/containers/heap_array.h"
#include "shaders/lights_interop.h"
#include "shaders/restir_interop.h"

namespace Render
{
struct LightAliasScratch
{
    LightAliasScratch() = default;

    explicit LightAliasScratch(Core::TlsfAllocator* alloc)
        : pdf(alloc, Core::AllocTag::Render, MAX_LIGHTS),
          scaled(alloc, Core::AllocTag::Render, MAX_LIGHTS),
          prob(alloc, Core::AllocTag::Render, MAX_LIGHTS),
          aliasIdx(alloc, Core::AllocTag::Render, MAX_LIGHTS),
          smallQ(alloc, Core::AllocTag::Render, MAX_LIGHTS),
          largeQ(alloc, Core::AllocTag::Render, MAX_LIGHTS),
          prevPower(alloc, Core::AllocTag::Render, MAX_LIGHTS),
          cachedEntries(alloc, Core::AllocTag::Render, MAX_LIGHTS) {}

    Core::HeapArray<float> pdf{};
    Core::HeapArray<float> scaled{};
    Core::HeapArray<float> prob{};
    Core::HeapArray<uint32_t> aliasIdx{};
    Core::HeapArray<uint32_t> smallQ{};
    Core::HeapArray<uint32_t> largeQ{};
    Core::HeapArray<float> prevPower{};
    Core::HeapArray<LightAliasEntry> cachedEntries{};
    uint32_t prevCount{UINT32_MAX};
};

/**
 * Builds a Vose alias table over the lights weighted by power (intensity * area * max(colorRGB) (see light_bvh.cpp).
 *
 * @param lights World-space light array.
 * @param count Number of lights (<= MAX_LIGHTS).
 * @param scratch Persistent MAX_LIGHTS-sized scratch, contents clobbered.
 * @param outEntries Receives `count` entries; caller allocates `count`. Write-only (may be write-combined upload memory). When total power is 0 (or count is 0) the table degenerates to a uniform 1/count sampler so the estimator stays well-defined.
 * @return count (number of valid entries), or 0 when count == 0.
 */
uint32_t BuildLightPowerAlias(const LightInfo* lights, uint32_t count, LightAliasScratch& scratch, LightAliasEntry* outEntries);
} // namespace Render

#endif // WILL_ENGINE_LIGHT_BVH_H
