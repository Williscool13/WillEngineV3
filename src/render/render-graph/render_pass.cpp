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
    blitImageWrites.push_back(resource);
    return *this;
}

RenderPass& RenderPass::WriteColorAttachment(const std::string& name, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (texInfo.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = texInfo;
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    colorAttachments.push_back(resource);
    return *this;
}

RenderPass& RenderPass::WriteDepthAttachment(const std::string& name, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (texInfo.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = texInfo;
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    assert(depthAttachment == nullptr && "Only one depth attachment per pass");

    depthAttachment = resource;
    return *this;
}

RenderPass& RenderPass::WriteBuffer(const std::string& name, VkPipelineStageFlags2 stages)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferWrites.push_back({resource, stages});
    return *this;
}

RenderPass& RenderPass::WriteTransferBuffer(const std::string& name, VkPipelineStageFlags2 stages)
{BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
    bufferWriteTransfer.push_back({resource, stages});
    return *this;
}

RenderPass& RenderPass::ReadDepthAttachment(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    assert(depthAttachment == nullptr && "Only one depth attachment per pass");

    depthAttachment = resource;
    depthReadOnly = true;
    return *this;
}

RenderPass& RenderPass::ReadStorageImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    storageImageReads.push_back(resource);
    return *this;
}

RenderPass& RenderPass::ReadSampledImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    sampledImageReads.push_back(resource);
    return *this;
}

RenderPass& RenderPass::ReadBlitImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    blitImageReads.push_back(resource);
    return *this;
}

RenderPass& RenderPass::ReadBuffer(const std::string& name, VkPipelineStageFlags2 stages)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferReads.push_back({resource, stages});
    return *this;
}

RenderPass& RenderPass::ReadTransferBuffer(const std::string& name, VkPipelineStageFlags2 stages)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");
    resource->accumulatedUsage |= VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
    bufferReadTransfer.push_back({resource, stages});
    return *this;
}

RenderPass& RenderPass::ReadIndirectBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    bufferReads.push_back({resource, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT});
    return *this;
}

RenderPass& RenderPass::Execute(std::function<void(VkCommandBuffer)> func)
{
    executeFunc = std::move(func);
    return *this;
}
} // Render
