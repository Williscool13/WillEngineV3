//
// Created by William on 2025-12-27.
//

#include "render_pass.h"

#include <cassert>

namespace Render
{
RenderPass::RenderPass(RenderGraph& renderGraph, std::string name)
    : graph(renderGraph), renderPassName(std::move(name))
{}

RenderPass& RenderPass::WriteStorageImage(const std::string& name, const TextureInfo info, bool bAsDescriptor)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    resource->usageType = TextureUsageType::Storage;
    if (bAsDescriptor && resource->descriptorIndex == UINT32_MAX) {
        resource->descriptorIndex = graph.GetStorageDescriptor();
    }

    if (info.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = info;
        }
        else {
            assert(resource->textureInfo.format == info.format && "Format mismatch");
            assert(resource->textureInfo.width == info.width && "Width mismatch");
            assert(resource->textureInfo.height == info.height && "Height mismatch");
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    writtenStorageImages.push_back(resource);
    return *this;
}

RenderPass& RenderPass::Execute(std::function<void(VkCommandBuffer)> func)
{
    executeFunc = std::move(func);
    return *this;
}
} // Render