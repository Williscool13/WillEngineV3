//
// Created by William on 2025-12-27.
//

#include "render_pass.h"

#include <cassert>

namespace Render
{
RenderPass::RenderPass(RenderGraph& renderGraph, StringID passId, VkPipelineStageFlags2 stages)
    : graph(renderGraph), renderPassId(std::move(passId)), stages(stages)
{}

RenderPass& RenderPass::WriteStorageImage(const StringID textureId, const TextureInfo texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);

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

RenderPass& RenderPass::WriteClearImage(const StringID textureId, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
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

RenderPass& RenderPass::WriteBlitImage(const StringID textureId, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
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

RenderPass& RenderPass::WriteCopyImage(const StringID textureId, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
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

RenderPass& RenderPass::WriteColorAttachment(const StringID textureId, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);

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

RenderPass& RenderPass::WriteDepthAttachment(const StringID textureId, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);

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

RenderPass& RenderPass::ReadWriteImage(const StringID textureId, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);

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

RenderPass& RenderPass::ReadDepthAttachment(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);

    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    assert(depthStencilAttachment == UINT_MAX && "Only one depth attachment per pass");

    depthStencilAttachment = resource->index;
    depthAccessType = DepthAccessType::Read;
    return *this;
}

RenderPass& RenderPass::ReadStorageImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    storageImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadSampledImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    sampledImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadBlitImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    blitImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadCopyImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    copyImageReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferWrites.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteTransferBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
    bufferTransferWrites.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadWriteDepthAttachment(const StringID bufferId, const TextureInfo& texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(bufferId);

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

RenderPass& RenderPass::ReadWriteBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferReadWrite.push_back(resource->index);
    return *this;
}


RenderPass& RenderPass::ReadBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");
    resource->accumulatedUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndexBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");
    resource->accumulatedUsage |= VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT;
    bufferIndexRead.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadTransferBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    assert(resource->bufferInfo.size > 0 && "Buffer not defined - import or create buffer first");
    resource->accumulatedUsage |= VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
    bufferTransferReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferIndirectReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectCountBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    resource->accumulatedUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferIndirectCountReads.push_back(resource->index);
    return *this;
}

RenderPass& RenderPass::Execute(std::function<void(VkCommandBuffer)> func)
{
    executeFunc = std::move(func);
    return *this;
}
} // Render
