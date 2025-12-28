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

RenderPass& RenderPass::WriteStorageImage(const std::string& name, const TextureInfo info)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

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

    storageImageWrites.push_back(resource);
    return *this;
}

RenderPass& RenderPass::WriteBlitImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert((resource->textureInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&"Texture must have TRANSFER_DST usage for blit writes");
    }

    blitImageWrites.push_back(resource);
    return *this;
}

RenderPass& RenderPass::WriteColorAttachment(const std::string& name, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (texInfo.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = texInfo;
            resource->textureInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    assert((resource->textureInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&"Texture must have COLOR_ATTACHMENT usage");

    colorAttachments.push_back(resource);
    return *this;
}

RenderPass& RenderPass::WriteDepthAttachment(const std::string& name, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (texInfo.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = texInfo;
            resource->textureInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    assert((resource->textureInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) && "Texture must have DEPTH_STENCIL_ATTACHMENT usage");
    assert(depthAttachment == nullptr && "Only one depth attachment per pass");

    depthAttachment = resource;
    return *this;
}

RenderPass& RenderPass::ReadDepthAttachment(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    assert((resource->textureInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) && "Texture must have DEPTH_STENCIL_ATTACHMENT usage");
    assert(depthAttachment == nullptr && "Only one depth attachment per pass");

    depthAttachment = resource;
    depthReadOnly = true;
    return *this;
}

RenderPass& RenderPass::ReadStorageImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert((resource->textureInfo.usage & VK_IMAGE_USAGE_STORAGE_BIT) && "Texture must have STORAGE usage for storage reads");
    }

    storageImageReads.push_back(resource);
    return *this;
}

RenderPass& RenderPass::ReadSampledImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert((resource->textureInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT) && "Texture must have SAMPLED usage for sampled reads");
    }

    sampledImageReads.push_back(resource);
    return *this;
}

RenderPass& RenderPass::ReadBlitImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert((resource->textureInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) && "Texture must have TRANSFER_SRC usage for blit reads");
    }

    blitImageReads.push_back(resource);
    return *this;
}

RenderPass& RenderPass::ReadBuffer(const std::string& name, VkPipelineStageFlags2 stages)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);

    // Validate that buffer has been defined (imported or created)
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");

    bufferReads.push_back({resource, stages});
    return *this;
}

RenderPass& RenderPass::Execute(std::function<void(VkCommandBuffer)> func)
{
    executeFunc = std::move(func);
    return *this;
}
} // Render
