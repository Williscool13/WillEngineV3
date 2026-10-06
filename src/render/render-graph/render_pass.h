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
    RenderPass(RenderGraph& renderGraph, StringID passId, VkPipelineStageFlags2 stages, RenderCategory category, Core::Arena* arena);

    // Textures
    RenderPass& WriteStorageImage(RDGTexture texture);
    RenderPass& WriteClearImage(RDGTexture texture);
    RenderPass& WriteBlitImage(RDGTexture texture);
    RenderPass& WriteCopyImage(RDGTexture texture);
    /** Color attachments have hard coded stage masks, so the pass does not need to specify stages for it. */
    RenderPass& WriteColorAttachment(RDGTexture texture);
    /** Depth attachments have hard coded stage masks, so the pass does not need to specify stages for it. */
    RenderPass& WriteDepthAttachment(RDGTexture texture);
    RenderPass& ReadWriteImage(RDGTexture texture);
    RenderPass& ReadDepthAttachment(RDGTexture texture);
    RenderPass& ReadStorageImage(RDGTexture texture);
    RenderPass& ReadSampledImage(RDGTexture texture);
    RenderPass& ReadBlitImage(RDGTexture texture);
    RenderPass& ReadCopyImage(RDGTexture texture);
    RenderPass& ReadWriteDepthAttachment(RDGTexture texture);

    // Buffers
    RenderPass& WriteBuffer(RDGBuffer buffer);
    RenderPass& WriteTransferBuffer(RDGBuffer buffer);
    RenderPass& ReadWriteBuffer(RDGBuffer buffer);
    RenderPass& ReadBuffer(RDGBuffer buffer);
    RenderPass& ReadIndexBuffer(RDGBuffer buffer);
    RenderPass& ReadTransferBuffer(RDGBuffer buffer);
    RenderPass& ReadIndirectBuffer(RDGBuffer buffer);
    RenderPass& ReadIndirectCountBuffer(RDGBuffer buffer);
    RenderPass& WriteTLASBuffer(RDGBuffer buffer);
    RenderPass& ReadTLASBuffer(RDGBuffer buffer);
    RenderPass& WriteScratchBuffer(RDGBuffer buffer);
    RenderPass& ReadASInputBuffer(RDGBuffer buffer);

    RenderPass& AsyncCompute()
    {
        bAsyncCompute = true;
        return *this;
    }

    template<typename F>
    RenderPass& Execute(F&& func)
    {
        executeFunc = Core::InlineFunction<void(VkCommandBuffer, VulkanContext*, RenderGraph&), 256>(std::forward<F>(func));
        return *this;
    }

    /** Whether this pass declared any read/write on the given logical texture/buffer index*/
    [[nodiscard]] bool DeclaresTexture(uint32_t textureIndex) const;
    [[nodiscard]] bool DeclaresBuffer(uint32_t bufferIndex) const;

    StringID renderPassId;
    VkPipelineStageFlags2 stages;
    RenderCategory category{RenderCategory::Untagged};
    bool bAsyncCompute{false};

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
    Core::ArenaVector<uint32_t> bufferTLASWrites;
    Core::ArenaVector<uint32_t> bufferTLASReads;
    Core::ArenaVector<uint32_t> bufferScratchWrites;
    Core::ArenaVector<uint32_t> bufferASInputReads;

    Core::ArenaVector<uint32_t> autoClearTextures;

    Core::InlineFunction<void(VkCommandBuffer, VulkanContext*, RenderGraph&), 256> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
