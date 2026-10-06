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
      storageImageReads(arena, 16), storageImageWrites(arena, 16),
      sampledImageReads(arena, 16), imageReadWrite(arena, 4),
      clearImageWrites(arena, 2), blitImageReads(arena, 2), blitImageWrites(arena, 2),
      copyImageReads(arena, 2), copyImageWrites(arena, 2),
      bufferReads(arena, 4), bufferWrites(arena, 4), bufferReadWrite(arena, 4),
      bufferTransferReads(arena, 2), bufferTransferWrites(arena, 2),
      bufferIndexRead(arena, 2), bufferIndirectReads(arena, 2), bufferIndirectCountReads(arena, 2),
      bufferTLASWrites(arena, 2), bufferTLASReads(arena, 2), bufferScratchWrites(arena, 2), bufferASInputReads(arena, 2),
      autoClearTextures(arena, 2)
{}

RenderPass& RenderPass::WriteStorageImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    resource.bWrittenThisFrame = true;
    storageImageWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteClearImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    resource.bWrittenThisFrame = true;
    clearImageWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteBlitImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    resource.bWrittenThisFrame = true;
    blitImageWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteCopyImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    resource.bWrittenThisFrame = true;
    copyImageWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteColorAttachment(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    resource.bWrittenThisFrame = true;
    colorAttachments.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteDepthAttachment(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    ENGINE_ASSERT(Renderer, depthStencilAttachment == UINT_MAX, "Only one depth attachment per pass");
    resource.bWrittenThisFrame = true;
    depthStencilAttachment = resource.index;
    depthAccessType |= DepthAccessType::Write;
    return *this;
}

RenderPass& RenderPass::ReadWriteImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    resource.bWrittenThisFrame = true;
    imageReadWrite.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadDepthAttachment(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    ENGINE_ASSERT(Renderer, depthStencilAttachment == UINT_MAX, "Only one depth attachment per pass");
    depthStencilAttachment = resource.index;
    depthAccessType = DepthAccessType::Read;
    return *this;
}

RenderPass& RenderPass::ReadStorageImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    storageImageReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadSampledImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    sampledImageReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadBlitImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    blitImageReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadCopyImage(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    copyImageReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadWriteDepthAttachment(RDGTexture texture)
{
    TextureResource& resource = graph.ResolveTexture(texture);
    ENGINE_ASSERT(Renderer, resource.textureInfo.format != VK_FORMAT_UNDEFINED, "[RDG] Texture '{}' declared on a pass before CreateTexture", resource.textureId.ToString());
    ENGINE_ASSERT(Renderer, depthStencilAttachment == UINT_MAX, "Only one depth attachment per pass");
    resource.bWrittenThisFrame = true;
    depthStencilAttachment = resource.index;
    depthAccessType = DepthAccessType::Read | DepthAccessType::Write;
    return *this;
}

RenderPass& RenderPass::WriteBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    resource.bWrittenThisFrame = true;
    bufferWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteTransferBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    resource.bWrittenThisFrame = true;
    bufferTransferWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadWriteBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    resource.bWrittenThisFrame = true;
    bufferReadWrite.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    ENGINE_ASSERT(Renderer, resource.bufferInfo.size > 0, "[RDG] Buffer '{}' declared on a pass before it was created or imported", resource.bufferId.ToString());
    bufferReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadIndexBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    ENGINE_ASSERT(Renderer, resource.bufferInfo.size > 0, "[RDG] Buffer '{}' declared on a pass before it was created or imported", resource.bufferId.ToString());
    bufferIndexRead.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadTransferBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    ENGINE_ASSERT(Renderer, resource.bufferInfo.size > 0, "[RDG] Buffer '{}' declared on a pass before it was created or imported", resource.bufferId.ToString());
    bufferTransferReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    bufferIndirectReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadIndirectCountBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    bufferIndirectCountReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteTLASBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    resource.bWrittenThisFrame = true;
    bufferTLASWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadTLASBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    ENGINE_ASSERT(Renderer, resource.bufferInfo.size > 0, "[RDG] Buffer '{}' declared on a pass before it was created or imported", resource.bufferId.ToString());
    bufferTLASReads.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::WriteScratchBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    resource.bWrittenThisFrame = true;
    bufferScratchWrites.PushBack(resource.index);
    return *this;
}

RenderPass& RenderPass::ReadASInputBuffer(RDGBuffer buffer)
{
    BufferResource& resource = graph.ResolveBuffer(buffer);
    ENGINE_ASSERT(Renderer, resource.bufferInfo.size > 0, "[RDG] Buffer '{}' declared on a pass before it was created or imported", resource.bufferId.ToString());
    bufferASInputReads.PushBack(resource.index);
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
