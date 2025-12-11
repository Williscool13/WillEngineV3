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
    , renderScale(scale)
    , scaledRenderExtents{
        static_cast<uint32_t>(width * scale + 0.5f),
        static_cast<uint32_t>(height * scale + 0.5f)
    }
    {}

    void RequestResize(uint32_t width, uint32_t height)
    {
        pendingResize = {width, height};
    }

    void ApplyResize()
    {
        if (!pendingResize) return;

        renderExtents[0] = pendingResize->width;
        renderExtents[1] = pendingResize->height;
        scaledRenderExtents[0] = static_cast<uint32_t>(renderExtents[0] * renderScale + 0.5f);
        scaledRenderExtents[1] = static_cast<uint32_t>(renderExtents[1] * renderScale + 0.5f);

        pendingResize.reset();
    }

    void UpdateScale(float newScale)
    {
        renderScale = newScale;
        scaledRenderExtents[0] = static_cast<uint32_t>(renderExtents[0] * renderScale + 0.5f);
        scaledRenderExtents[1] = static_cast<uint32_t>(renderExtents[1] * renderScale + 0.5f);
    }

    [[nodiscard]] bool HasPendingResize() const { return pendingResize.has_value(); }
    [[nodiscard]] std::array<uint32_t, 2> GetExtent() const { return renderExtents; }
    [[nodiscard]] std::array<uint32_t, 2> GetScaledExtent() const { return scaledRenderExtents; }

    [[nodiscard]] float GetAspectRatio() const
    {
        return renderExtents[0] / static_cast<float>(renderExtents[1]);
    }

    [[nodiscard]] glm::vec2 GetTexelSize() const
    {
        return {1.0f / renderExtents[0], 1.0f / renderExtents[1]};
    }

private:
    std::array<uint32_t, 2> renderExtents;
    std::array<uint32_t, 2> scaledRenderExtents;
    float renderScale;

    struct PendingResize { uint32_t width, height; };
    std::optional<PendingResize> pendingResize;
};
} // Render

#endif //WILL_ENGINE_VK_RENDER_EXTENTS_H