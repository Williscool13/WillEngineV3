//
// Created by William on 2026-05-06.
//

#ifndef WILL_ENGINE_RENDERER_STATISTICS_H
#define WILL_ENGINE_RENDERER_STATISTICS_H

#include <mutex>
#include <cstdint>

#include "render/render-graph/render_graph_resources.h"

namespace Render
{
struct RadianceCacheStatistics
{
    uint32_t occupiedSlots{};
    uint32_t cellsCarried{};
    uint32_t cellsEvicted{};
    uint32_t insertsFailed{};
    uint32_t cellsDumped{};
    uint32_t cellsDark{};
    uint32_t cellsShaded{};
};

struct RendererStatistics
{
    // Geometry pass
    uint32_t visibleMeshletCount{};
    uint32_t culledInstanceFrustum{};
    uint32_t culledInstanceContribution{};
    uint32_t culledInstanceOcclusion{};
    uint32_t culledMeshletFrustum{};
    uint32_t culledMeshletCone{};
    uint32_t culledMeshletContribution{};
    uint32_t culledMeshletOcclusion{};
    uint32_t meshletRegionExpanded[4]{};
    uint32_t meshletRegionVisible[4]{};

    // Visibility bucketing
    uint32_t shadingDispatches{};
    uint32_t lightingDispatches{};

    // Radiance cache occupancy (multi-frame readback latency)
    RadianceCacheStatistics radianceCache{};

    // Pipeline statistics (whole-frame query)
    uint64_t meshInvocations{};
    uint64_t fragmentInvocations{};
    uint64_t computeInvocations{};
    uint64_t clippingInvocations{};
    uint64_t clippingPrimitives{};

    // Per-category GPU pass timing (always-on)
    GPUProfileSnapshot gpuProfile{};

    // Render-thread wall interval between frames
    float wallFrameMs{};
    // gpuProfile.spanMs
    float gpuSpanMs{};
};

/**
 * Accumulates renderer statistics on the render thread each frame, then publishes
 * them atomically so any thread can read a consistent snapshot.
 *
 * Render thread: write into `scratch` freely, call `Publish()` once per frame when done.
 * Any thread:    call `GetPublished()` to retrieve the latest committed snapshot.
 */
struct RendererStatisticsManager
{
    RendererStatistics scratch{};

    void Publish()
    {
        std::lock_guard lock(mutex);
        published = scratch;
        scratch = {};
    }

    RendererStatistics GetPublished()
    {
        std::lock_guard lock(mutex);
        return published;
    }

private:
    std::mutex mutex{};
    RendererStatistics published{};
};
} // Render

#endif //WILL_ENGINE_RENDERER_STATISTICS_H
