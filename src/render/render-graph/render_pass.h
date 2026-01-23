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
    RenderPass(RenderGraph& renderGraph, std::string name, VkPipelineStageFlags2 stages);

    // Write
    RenderPass& WriteStorageImage(const std::string& name, TextureInfo texInfo = {});

    RenderPass& WriteClearImage(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& WriteBlitImage(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& WriteCopyImage(const std::string& name, const TextureInfo& texInfo = {});

    /**
     * Color attachments have hard coded stage masks, so the pass does not need to specify stages for it.
     * @param name
     * @param texInfo
     * @return
     */
    RenderPass& WriteColorAttachment(const std::string& name, const TextureInfo& texInfo = {});

    /**
     * Depth attachments have hard coded stage masks, so the pass does not need to specify stages for it.
     * @param name
     * @param texInfo
     * @return
     */
    RenderPass& WriteDepthAttachment(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& WriteBuffer(const std::string& name);

    RenderPass& WriteTransferBuffer(const std::string& name);

    RenderPass& ReadWriteDepthAttachment(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& ReadWriteBuffer(const std::string& name);

    RenderPass& ReadWriteImage(const std::string& name, const TextureInfo& texInfo = {});

    // Read
    RenderPass& ReadDepthAttachment(const std::string& name);

    RenderPass& ReadStorageImage(const std::string& name);

    RenderPass& ReadSampledImage(const std::string& name);

    RenderPass& ReadBlitImage(const std::string& name);

    RenderPass& ReadCopyImage(const std::string& name);

    RenderPass& ReadBuffer(const std::string& name);

    RenderPass& ReadTransferBuffer(const std::string& name);

    RenderPass& ReadIndirectBuffer(const std::string& name);

    RenderPass& ReadIndirectCountBuffer(const std::string& name);

    RenderPass& Execute(std::function<void(VkCommandBuffer)> func);

    RenderGraph& graph;
    std::string renderPassName;
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
    std::vector<uint32_t> bufferReadTransfer;
    std::vector<uint32_t> bufferWriteTransfer;
    std::vector<uint32_t> bufferIndirectReads;
    std::vector<uint32_t> bufferIndirectCountReads;

    std::function<void(VkCommandBuffer_T*)> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
