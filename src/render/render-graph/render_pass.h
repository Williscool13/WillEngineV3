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

    std::vector<TextureResource*> colorAttachments;
    TextureResource* depthAttachment = nullptr;
    bool depthReadOnly = false;

    std::vector<TextureResource*> storageImageReads;
    std::vector<TextureResource*> storageImageWrites;
    std::vector<TextureResource*> sampledImageReads;
    std::vector<TextureResource*> imageReadWrite;
    std::vector<TextureResource*> clearImageWrites;
    std::vector<TextureResource*> blitImageReads;
    std::vector<TextureResource*> blitImageWrites;
    std::vector<TextureResource*> copyImageReads;
    std::vector<TextureResource*> copyImageWrites;

    std::vector<BufferResource*> bufferReads;
    std::vector<BufferResource*> bufferWrites;
    std::vector<BufferResource*> bufferReadTransfer;
    std::vector<BufferResource*> bufferWriteTransfer;
    std::vector<BufferResource*> bufferIndirectReads;
    std::vector<BufferResource*> bufferIndirectCountReads;

    std::function<void(VkCommandBuffer_T*)> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
