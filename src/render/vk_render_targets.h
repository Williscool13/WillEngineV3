//
// Created by William on 2025-10-19.
//

#ifndef WILLENGINETESTBED_RENDER_TARGETS_H
#define WILLENGINETESTBED_RENDER_TARGETS_H
#include <algorithm>

#include "vk_resources.h"

namespace Render
{
struct VulkanContext;

struct RenderTargets
{

    RenderTargets(VulkanContext* context, uint32_t width, uint32_t height);

    ~RenderTargets();

    RenderTargets(const RenderTargets&) = delete;
    RenderTargets& operator=(const RenderTargets&) = delete;

    RenderTargets(RenderTargets&& other) noexcept
        : drawImage(std::move(other.drawImage))
        , drawImageView(std::move(other.drawImageView))
        , depthImage(std::move(other.depthImage))
        , depthImageView(std::move(other.depthImageView))
        , context(other.context)
    {
        other.context = nullptr;
    }

    RenderTargets& operator=(RenderTargets&& other) noexcept
    {
        if (this != &other) {
            drawImage = std::move(other.drawImage);
            drawImageView = std::move(other.drawImageView);
            depthImage = std::move(other.depthImage);
            depthImageView = std::move(other.depthImageView);
            context = other.context;
            other.context = nullptr;
        }
        return *this;
    }

    void Create(uint32_t width, uint32_t height);

    void Recreate(uint32_t width, uint32_t height);

    AllocatedImage drawImage{};
    ImageView drawImageView{};
    AllocatedImage depthImage{};
    ImageView depthImageView{};

private:
    VulkanContext* context{};
};
} // Renderer

#endif //WILLENGINETESTBED_RENDER_TARGETS_H