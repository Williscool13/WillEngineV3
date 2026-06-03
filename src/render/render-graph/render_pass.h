//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_PASS_H
#define WILL_ENGINE_RENDER_PASS_H
#include "render_graph.h"
#include "core/containers/arena_vector.h"
#include "core/containers/inline_function.h"

namespace Render
{
struct TextureResource;

class RenderPass
{
public:
    RenderPass(RenderGraph& renderGraph, StringID passId, VkPipelineStageFlags2 stages, Core::Arena* arena);

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

    StringID renderPassId;
    VkPipelineStageFlags2 stages;

public: // DAG compile-time fields
    uint32_t passIndex{UINT_MAX};
    uint32_t waveIndex{0};
    uint32_t inDegree{0};
    Core::ArenaVector<uint32_t> inEdges;
    Core::ArenaVector<uint32_t> outEdges;

private:
    friend class RenderGraph;
    friend class RenderGraphInspector;
    RenderGraph& graph;

    Core::ArenaVector<uint32_t> colorAttachments;
    uint32_t depthStencilAttachment{UINT_MAX};
    DepthAccessType depthAccessType{0};

    Core::ArenaVector<uint32_t> storageImageReads;
    Core::ArenaVector<uint32_t> storageImageWrites;
    Core::ArenaVector<uint32_t> sampledImageReads;
    Core::ArenaVector<uint32_t> imageReadWrite;
    Core::ArenaVector<uint32_t> clearImageWrites;
    Core::ArenaVector<uint32_t> blitImageReads;
    Core::ArenaVector<uint32_t> blitImageWrites;
    Core::ArenaVector<uint32_t> copyImageReads;
    Core::ArenaVector<uint32_t> copyImageWrites;

    Core::ArenaVector<uint32_t> bufferReads;
    Core::ArenaVector<uint32_t> bufferWrites;
    Core::ArenaVector<uint32_t> bufferReadWrite;
    Core::ArenaVector<uint32_t> bufferTransferReads;
    Core::ArenaVector<uint32_t> bufferTransferWrites;
    Core::ArenaVector<uint32_t> bufferIndexRead;
    Core::ArenaVector<uint32_t> bufferIndirectReads;
    Core::ArenaVector<uint32_t> bufferIndirectCountReads;

    Core::ArenaVector<uint32_t> autoClearTextures;

    Core::InlineFunction<void(VkCommandBuffer), 128> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
