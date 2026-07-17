//
// Created by William on 2025-12-27.
//

#include "render_pass.h"

#include <cassert>
#include "engine/logging/engine_assert.h"

namespace Render
{
RenderPass::RenderPass(RenderGraph& renderGraph, StringID passId, VkPipelineStageFlags2 stages, RenderCategory category, Core::Arena* arena)
    : graph(renderGraph), renderPassId(std::move(passId)), stages(stages), category(category),
      inEdges(arena, 4), outEdges(arena, 4),
      colorAttachments(arena, 4),
      storageImageReads(arena, 4), storageImageWrites(arena, 4),
      sampledImageReads(arena, 4), imageReadWrite(arena, 4),
      clearImageWrites(arena, 2), blitImageReads(arena, 2), blitImageWrites(arena, 2),
      copyImageReads(arena, 2), copyImageWrites(arena, 2),
      bufferReads(arena, 4), bufferWrites(arena, 4), bufferReadWrite(arena, 4),
      bufferTransferReads(arena, 2), bufferTransferWrites(arena, 2),
      bufferIndexRead(arena, 2), bufferIndirectReads(arena, 2), bufferIndirectCountReads(arena, 2),
      bufferTLASWrites(arena, 2), bufferTLASReads(arena, 2), bufferScratchWrites(arena, 2), bufferASInputReads(arena, 2),
      autoClearTextures(arena, 2)
{}

RenderPass& RenderPass::WriteStorageImage(const StringID textureId, const TextureInfo texInfo)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);

    if (texInfo.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = texInfo;
        }
        else {
            ENGINE_ASSERT(Renderer, resource->textureInfo.format == texInfo.format, "Format mismatch");
            ENGINE_ASSERT(Renderer, resource->textureInfo.width == texInfo.width, "Width mismatch");
            ENGINE_ASSERT(Renderer, resource->textureInfo.height == texInfo.height, "Height mismatch");
        }
    }
    else {
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }

    storageImageWrites.PushBack(resource->index);
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
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }
    clearImageWrites.PushBack(resource->index);
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
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }
    blitImageWrites.PushBack(resource->index);
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
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }
    copyImageWrites.PushBack(resource->index);
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
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }

    colorAttachments.PushBack(resource->index);
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
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }

    ENGINE_ASSERT(Renderer, depthStencilAttachment == UINT_MAX, "Only one depth attachment per pass");

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
            ENGINE_ASSERT(Renderer, resource->textureInfo.format == texInfo.format, "Format mismatch");
            ENGINE_ASSERT(Renderer, resource->textureInfo.width == texInfo.width, "Width mismatch");
            ENGINE_ASSERT(Renderer, resource->textureInfo.height == texInfo.height, "Height mismatch");
        }
    }
    else {
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }

    imageReadWrite.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadDepthAttachment(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);

    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }

    ENGINE_ASSERT(Renderer, depthStencilAttachment == UINT_MAX, "Only one depth attachment per pass");

    depthStencilAttachment = resource->index;
    depthAccessType = DepthAccessType::Read;
    return *this;
}

RenderPass& RenderPass::ReadStorageImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    storageImageReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadSampledImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    sampledImageReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadBlitImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    blitImageReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadCopyImage(const StringID textureId)
{
    TextureResource* resource = graph.GetOrCreateTexture(textureId);
    copyImageReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    bufferWrites.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteTransferBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    bufferTransferWrites.PushBack(resource->index);
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
        ENGINE_ASSERT(Renderer, resource->textureInfo.format != VK_FORMAT_UNDEFINED, "Texture not defined - provide TextureInfo on first use");
    }

    ENGINE_ASSERT(Renderer, depthStencilAttachment == UINT_MAX, "Only one depth attachment per pass");

    depthStencilAttachment = resource->index;
    depthAccessType = DepthAccessType::Read | DepthAccessType::Write;
    return *this;
}

RenderPass& RenderPass::ReadWriteBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    bufferReadWrite.PushBack(resource->index);
    return *this;
}


RenderPass& RenderPass::ReadBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    ENGINE_ASSERT(Renderer, resource->bufferInfo.size > 0, "Buffer not defined - import or create buffer first");
    bufferReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndexBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    ENGINE_ASSERT(Renderer, resource->bufferInfo.size > 0, "Buffer not defined - import or create buffer first");
    bufferIndexRead.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadTransferBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    ENGINE_ASSERT(Renderer, resource->bufferInfo.size > 0, "Buffer not defined - import or create buffer first");
    bufferTransferReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    bufferIndirectReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectCountBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    bufferIndirectCountReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteTLASBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    bufferTLASWrites.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadTLASBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    ENGINE_ASSERT(Renderer, resource->bufferInfo.size > 0, "Buffer not defined - import or create buffer first");
    bufferTLASReads.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::WriteScratchBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    bufferScratchWrites.PushBack(resource->index);
    return *this;
}

RenderPass& RenderPass::ReadASInputBuffer(const StringID bufferId)
{
    BufferResource* resource = graph.GetOrCreateBuffer(bufferId);
    ENGINE_ASSERT(Renderer, resource->bufferInfo.size > 0, "Buffer not defined - import or create buffer first");
    bufferASInputReads.PushBack(resource->index);
    return *this;
}

static bool ListContains(const Core::ArenaVector<uint32_t>& list, uint32_t value)
{
    for (uint32_t entry : list) {
        if (entry == value) {
            return true;
        }
    }
    return false;
}

bool RenderPass::DeclaresTexture(uint32_t textureIndex) const
{
    if (depthStencilAttachment == textureIndex) {
        return true;
    }
    return ListContains(colorAttachments, textureIndex)
           || ListContains(storageImageReads, textureIndex)
           || ListContains(storageImageWrites, textureIndex)
           || ListContains(sampledImageReads, textureIndex)
           || ListContains(imageReadWrite, textureIndex)
           || ListContains(clearImageWrites, textureIndex)
           || ListContains(blitImageReads, textureIndex)
           || ListContains(blitImageWrites, textureIndex)
           || ListContains(copyImageReads, textureIndex)
           || ListContains(copyImageWrites, textureIndex)
           || ListContains(autoClearTextures, textureIndex);
}

bool RenderPass::DeclaresBuffer(uint32_t bufferIndex) const
{
    return ListContains(bufferReads, bufferIndex)
           || ListContains(bufferWrites, bufferIndex)
           || ListContains(bufferReadWrite, bufferIndex)
           || ListContains(bufferTransferReads, bufferIndex)
           || ListContains(bufferTransferWrites, bufferIndex)
           || ListContains(bufferIndexRead, bufferIndex)
           || ListContains(bufferIndirectReads, bufferIndex)
           || ListContains(bufferIndirectCountReads, bufferIndex)
           || ListContains(bufferTLASWrites, bufferIndex)
           || ListContains(bufferTLASReads, bufferIndex)
           || ListContains(bufferScratchWrites, bufferIndex)
           || ListContains(bufferASInputReads, bufferIndex);
}
} // Render
