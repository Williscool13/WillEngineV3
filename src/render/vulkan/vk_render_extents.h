//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_RENDER_EXTENTS_H
#define WILL_ENGINE_VK_RENDER_EXTENTS_H

#include <array>
#include <optional>
#include <glm/glm.hpp>

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
        renderExtents[0] = width;
        renderExtents[1] = height;
    }

    void ApplyViewportResize(uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height)
    {
        viewportOffset[0] = offsetX;
        viewportOffset[1] = offsetY;
        viewportExtents[0] = width;
        viewportExtents[1] = height;
        recomputeScaled();
    }

    void UpdateScale(float newScale)
    {
        renderScale = newScale;
        recomputeScaled();
    }

    // Swapchain size
    [[nodiscard]] std::array<uint32_t, 2> GetExtent() const { return renderExtents; }

    // Viewport panel size (blit destination)
    [[nodiscard]] std::array<uint32_t, 2> GetViewportExtent() const { return viewportExtents; }
    [[nodiscard]] std::array<uint32_t, 2> GetViewportOffset() const { return viewportOffset; }

    // Actual render target size
    [[nodiscard]] std::array<uint32_t, 2> GetScaledExtent() const { return scaledViewportExtent; }

    [[nodiscard]] float GetAspectRatio() const
    {
        return static_cast<float>(viewportExtents[0]) / static_cast<float>(viewportExtents[1]);
    }

    [[nodiscard]] glm::vec2 GetTexelSize() const
    {
        return {1.0f / static_cast<float>(scaledViewportExtent[0]),
                1.0f / static_cast<float>(scaledViewportExtent[1])};
    }

private:
    void recomputeScaled()
    {
        scaledViewportExtent[0] = static_cast<uint32_t>(std::lround(static_cast<float>(viewportExtents[0]) * renderScale));
        scaledViewportExtent[1] = static_cast<uint32_t>(std::lround(static_cast<float>(viewportExtents[1]) * renderScale));
    }

    std::array<uint32_t, 2> renderExtents;
    std::array<uint32_t, 2> viewportExtents;
    std::array<uint32_t, 2> viewportOffset;
    float renderScale;
    std::array<uint32_t, 2> scaledViewportExtent;
};
} // Render

#endif //WILL_ENGINE_VK_RENDER_EXTENTS_H
