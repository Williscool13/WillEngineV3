//
// Created by William on 2025-12-11.
//

#include "vk_render_targets.h"

#include "vk_config.h"
#include "vk_context.h"
#include "vk_helpers.h"

namespace Render
{
RenderTargets::RenderTargets(VulkanContext* context, uint32_t width, uint32_t height)
    : context(context)
{
    Create(width, height);
}

RenderTargets::~RenderTargets() = default;

void RenderTargets::Create(uint32_t width, uint32_t height)
{
    //
    {
        VkImageUsageFlags drawImageUsages{};
        drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
        drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageCreateInfo drawImageCreateInfo = VkHelpers::ImageCreateInfo(COLOR_ATTACHMENT_FORMAT, {width, height, 1}, drawImageUsages);
        colorTarget = AllocatedImage::CreateAllocatedImage(context, drawImageCreateInfo);

        VkImageViewCreateInfo drawImageViewCreateInfo = VkHelpers::ImageViewCreateInfo(colorTarget.handle, COLOR_ATTACHMENT_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
        colorTargetView = ImageView::CreateImageView(context, drawImageViewCreateInfo);
    }

    //
    {
        VkImageUsageFlags depthImageUsages{};
        depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        depthImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        depthImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageCreateInfo depthImageCreateInfo = VkHelpers::ImageCreateInfo(DEPTH_ATTACHMENT_FORMAT, {width, height, 1}, depthImageUsages);
        depthTarget = AllocatedImage::CreateAllocatedImage(context, depthImageCreateInfo);

        VkImageViewCreateInfo depthImageViewCreateInfo = VkHelpers::ImageViewCreateInfo(depthTarget.handle, DEPTH_ATTACHMENT_FORMAT, VK_IMAGE_ASPECT_DEPTH_BIT);
        depthTargetView = ImageView::CreateImageView(context, depthImageViewCreateInfo);
    }
}
void RenderTargets::Recreate(uint32_t width, uint32_t height)
{
    Create(width, height);
}
} // Renderer
