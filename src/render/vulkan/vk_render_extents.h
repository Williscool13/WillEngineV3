//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_RENDER_EXTENTS_H
#define WILL_ENGINE_VK_RENDER_EXTENTS_H

#include <glm/glm.hpp>

#include "core/containers/array.h"
#include "core/types/extent.h"

namespace Render
{
struct RenderExtents
{
    RenderExtents(uint32_t width, uint32_t height, float scale)
        : renderExtents{width, height}
        , viewportExtents{width, height}
        , viewportOffset{0, 0}
        , renderScale(scale)
        , scaledViewportExtent{
              static_cast<uint32_t>(std::lround(static_cast<float>(width) * scale)),
              static_cast<uint32_t>(std::lround(static_cast<float>(height) * scale))
          }
    {}

    void ApplyResize(uint32_t width, uint32_t height)
    {
        renderExtents.width = width;
        renderExtents.height = height;
    }

    void ApplyViewportResize(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height)
    {
        viewportOffset[0] = offsetX;
        viewportOffset[1] = offsetY;
        viewportExtents.width = width;
        viewportExtents.height = height;
        RecomputeScaled();
    }

    void UpdateScale(float newScale)
    {
        renderScale = newScale;
        RecomputeScaled();
    }

    // Swapchain size
    [[nodiscard]] Core::Extent2D GetExtent() const { return renderExtents; }

    // Viewport panel size (blit destination)
    [[nodiscard]] Core::Extent2D GetViewportExtent() const { return viewportExtents; }
    [[nodiscard]] Core::Array<uint32_t, 2> GetViewportOffset() const { return viewportOffset; }

    // Actual render target size
    [[nodiscard]] Core::Extent2D GetScaledExtent() const { return scaledViewportExtent; }

    [[nodiscard]] float GetAspectRatio() const
    {
        return static_cast<float>(viewportExtents.width) / static_cast<float>(viewportExtents.height);
    }

    [[nodiscard]] glm::vec2 GetTexelSize() const
    {
        return {1.0f / static_cast<float>(scaledViewportExtent.width),
                1.0f / static_cast<float>(scaledViewportExtent.height)};
    }

private:
    void RecomputeScaled()
    {
        scaledViewportExtent.width = static_cast<uint32_t>(std::lround(static_cast<float>(viewportExtents.width) * renderScale));
        scaledViewportExtent.height = static_cast<uint32_t>(std::lround(static_cast<float>(viewportExtents.height) * renderScale));
    }

    Core::Extent2D renderExtents;
    Core::Extent2D viewportExtents;
    Core::Array<uint32_t, 2> viewportOffset;
    float renderScale;
    Core::Extent2D scaledViewportExtent;
};
} // Render

#endif //WILL_ENGINE_VK_RENDER_EXTENTS_H
