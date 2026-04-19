//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_PASS_H
#define WILL_ENGINE_RENDER_PASS_H
#include "render_graph.h"
#include "core/containers/inline_function.h"
#include "core/containers/inline_vector.h"

namespace Render
{
struct TextureResource;

class RenderPass
{
public:
    RenderPass(RenderGraph& renderGraph, StringID passId, VkPipelineStageFlags2 stages);

    // Write
    RenderPass& WriteStorageImage(StringID textureId, TextureInfo texInfo = {});

    RenderPass& WriteClearImage(StringID textureId, const TextureInfo& texInfo = {});

    RenderPass& WriteBlitImage(StringID textureId, const TextureInfo& texInfo = {});

    RenderPass& WriteCopyImage(StringID textureId, const TextureInfo& texInfo = {});

    /**
     * Color attachments have hard coded stage masks, so the pass does not need to specify stages for it.
     * @param textureId
     * @param texInfo
     * @return
     */
    RenderPass& WriteColorAttachment(StringID textureId, const TextureInfo& texInfo = {});

    /**
     * Depth attachments have hard coded stage masks, so the pass does not need to specify stages for it.
     * @param textureId
     * @param texInfo
     * @return
     */
    RenderPass& WriteDepthAttachment(StringID textureId, const TextureInfo& texInfo = {});

    RenderPass& ReadWriteImage(StringID name, const TextureInfo& texInfo = {});

    RenderPass& ReadDepthAttachment(StringID textureId);

    RenderPass& ReadStorageImage(StringID textureId);

    RenderPass& ReadSampledImage(StringID textureId);

    RenderPass& ReadBlitImage(StringID textureId);

    RenderPass& ReadCopyImage(StringID textureId);

    // Buffers
    RenderPass& WriteBuffer(StringID bufferId);

    RenderPass& WriteTransferBuffer(StringID bufferId);

    RenderPass& ReadWriteDepthAttachment(StringID bufferId, const TextureInfo& texInfo = {});

    RenderPass& ReadWriteBuffer(StringID bufferId);

    RenderPass& ReadBuffer(StringID bufferId);

    RenderPass& ReadIndexBuffer(StringID bufferId);

    RenderPass& ReadTransferBuffer(StringID bufferId);

    RenderPass& ReadIndirectBuffer(StringID bufferId);

    RenderPass& ReadIndirectCountBuffer(StringID bufferId);


    template<typename F>
    RenderPass& Execute(F&& func)
    {
        executeFunc = Core::InlineFunction<void(VkCommandBuffer), 128>(std::forward<F>(func));
        return *this;
    }

    RenderGraph& graph;
    StringID renderPassId;
    VkPipelineStageFlags2 stages;

private:
    friend class RenderGraph;

    Core::InlineVector<uint32_t, 8> colorAttachments{};
    uint32_t depthStencilAttachment{UINT_MAX};
    DepthAccessType depthAccessType{0};

    Core::InlineVector<uint32_t, 32> storageImageReads;
    Core::InlineVector<uint32_t, 32> storageImageWrites;
    Core::InlineVector<uint32_t, 32> sampledImageReads;
    Core::InlineVector<uint32_t, 32> imageReadWrite;
    Core::InlineVector<uint32_t, 16> clearImageWrites;
    Core::InlineVector<uint32_t, 16> blitImageReads;
    Core::InlineVector<uint32_t, 16> blitImageWrites;
    Core::InlineVector<uint32_t, 16> copyImageReads;
    Core::InlineVector<uint32_t, 16> copyImageWrites;

    Core::InlineVector<uint32_t, 32> bufferReads;
    Core::InlineVector<uint32_t, 32> bufferWrites;
    Core::InlineVector<uint32_t, 32> bufferReadWrite;
    Core::InlineVector<uint32_t, 32> bufferTransferReads;
    Core::InlineVector<uint32_t, 32> bufferTransferWrites;
    Core::InlineVector<uint32_t, 16> bufferIndexRead;
    Core::InlineVector<uint32_t, 16> bufferIndirectReads;
    Core::InlineVector<uint32_t, 16> bufferIndirectCountReads;

    Core::InlineVector<uint32_t, 8> autoClearTextures{};

    Core::InlineFunction<void(VkCommandBuffer), 128> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
