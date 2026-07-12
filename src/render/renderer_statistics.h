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
struct RendererStatistics
{
    // Geometry pass
    uint32_t visibleMeshletCount{};

    // Visibility bucketing
    uint32_t shadingDispatches{};
    uint32_t lightingDispatches{};

    // Pipeline statistics (whole-frame query)
    uint64_t meshInvocations{};
    uint64_t fragmentInvocations{};
    uint64_t computeInvocations{};
    uint64_t clippingInvocations{};
    uint64_t clippingPrimitives{};

    // Per-category GPU pass timing (always-on)
    GPUProfileSnapshot gpuProfile{};
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
