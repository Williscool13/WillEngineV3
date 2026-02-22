//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_PASS_H
#define WILL_ENGINE_RENDER_PASS_H
#include <string>
#include <vector>

#include "render_graph.h"

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


    RenderPass& Execute(std::function<void(VkCommandBuffer)> func);

    RenderGraph& graph;
    StringID renderPassId;
    VkPipelineStageFlags2 stages;

private:
    friend class RenderGraph;

    std::vector<uint32_t> colorAttachments{};
    uint32_t depthStencilAttachment{UINT_MAX};
    DepthAccessType depthAccessType{0};


    std::vector<uint32_t> storageImageReads;
    std::vector<uint32_t> storageImageWrites;
    std::vector<uint32_t> sampledImageReads;
    std::vector<uint32_t> imageReadWrite;
    std::vector<uint32_t> clearImageWrites;
    std::vector<uint32_t> blitImageReads;
    std::vector<uint32_t> blitImageWrites;
    std::vector<uint32_t> copyImageReads;
    std::vector<uint32_t> copyImageWrites;

    std::vector<uint32_t> bufferReads;
    std::vector<uint32_t> bufferWrites;
    std::vector<uint32_t> bufferReadWrite;
    std::vector<uint32_t> bufferTransferReads;
    std::vector<uint32_t> bufferTransferWrites;
    std::vector<uint32_t> bufferIndexRead;
    std::vector<uint32_t> bufferIndirectReads;
    std::vector<uint32_t> bufferIndirectCountReads;

    std::function<void(VkCommandBuffer_T*)> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
