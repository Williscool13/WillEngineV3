//
// Created by William on 2025-12-27.
//

#include "render_pass.h"

#include <cassert>

namespace Render
{
RenderPass::RenderPass(RenderGraph& renderGraph, std::string name, VkPipelineStageFlags2 stages)
    : graph(renderGraph), renderPassName(std::move(name)), stages(stages)
{}

RenderPass& RenderPass::WriteStorageImage(const std::string& name, const TextureInfo texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (texInfo.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = texInfo;
        }
        else {
            assert(resource->textureInfo.format == texInfo.format && "Format mismatch");
            assert(resource->textureInfo.width == texInfo.width && "Width mismatch");
            assert(resource->textureInfo.height == texInfo.height && "Height mismatch");
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    storageImageWrites.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteClearImage(const std::string& name, const TextureInfo& texInfo)
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
    clearImageWrites.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteBlitImage(const std::string& name, const TextureInfo& texInfo)
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
    blitImageWrites.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteCopyImage(const std::string& name, const TextureInfo& texInfo)
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
    copyImageWrites.push_back(resource->index);
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

    colorAttachments.push_back(resource->index);
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

    assert(depthStencilAttachment == UINT_MAX && "Only one depth attachment per pass");

    depthStencilAttachment = resource->index;
    depthAccessType |= DepthAccessType::Write;
    return *this;
}

RenderPass& RenderPass::WriteBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferWrites.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteTransferBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
    bufferWriteTransfer.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadWriteDepthAttachment(const std::string& name, const TextureInfo& texInfo)
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

    assert(depthStencilAttachment == UINT_MAX && "Only one depth attachment per pass");

    depthStencilAttachment = resource->index;
    depthAccessType = DepthAccessType::Read | DepthAccessType::Write;
    return *this;
}

RenderPass& RenderPass::ReadWriteBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferReadWrite.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadWriteImage(const std::string& name, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (texInfo.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = texInfo;
        }
        else {
            assert(resource->textureInfo.format == texInfo.format && "Format mismatch");
            assert(resource->textureInfo.width == texInfo.width && "Width mismatch");
            assert(resource->textureInfo.height == texInfo.height && "Height mismatch");
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    imageReadWrite.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadDepthAttachment(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    assert(depthStencilAttachment == UINT_MAX && "Only one depth attachment per pass");

    depthStencilAttachment = resource->index;
    depthAccessType = DepthAccessType::Read;
    return *this;
}

RenderPass& RenderPass::ReadStorageImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    storageImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadSampledImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    sampledImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadBlitImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    blitImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadCopyImage(const std::string& name)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);
    copyImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadTransferBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");
    resource->accumulatedUsage |= VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
    bufferReadTransfer.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferIndirectReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectCountBuffer(const std::string& name)
{
    BufferResource* resource = graph.GetOrCreateBuffer(name);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    bufferIndirectCountReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::Execute(std::function<void(VkCommandBuffer)> func)
{
    executeFunc = std::move(func);
    return *this;
}
} // Render
