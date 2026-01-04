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
    RenderPass(RenderGraph& renderGraph, std::string name);

    // Write
    RenderPass& WriteStorageImage(const std::string& name, TextureInfo info = {});

    RenderPass& WriteBlitImage(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& WriteColorAttachment(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& WriteDepthAttachment(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& WriteBuffer(const std::string& name, VkPipelineStageFlags2 stages);

    RenderPass& WriteTransferBuffer(const std::string& name, VkPipelineStageFlags2 stages);


    // Read
    RenderPass& ReadDepthAttachment(const std::string& name);

    RenderPass& ReadStorageImage(const std::string& name);

    RenderPass& ReadSampledImage(const std::string& name);

    RenderPass& ReadBlitImage(const std::string& name);

    RenderPass& ReadBuffer(const std::string& name, VkPipelineStageFlags2 stages);

    RenderPass& ReadTransferBuffer(const std::string& name, VkPipelineStageFlags2 stages);

    RenderPass& ReadIndirectBuffer(const std::string& name, VkPipelineStageFlags2 stages);

    RenderPass& ReadIndirectCountBuffer(const std::string& name, VkPipelineStageFlags2 stages);

    RenderPass& Execute(std::function<void(VkCommandBuffer)> func);

private:
    friend class RenderGraph;
    RenderGraph& graph;
    std::string renderPassName;

    struct BufferAccess
    {
        BufferResource* resource;
        VkPipelineStageFlags2 stages;
    };

    std::vector<TextureResource*> colorAttachments;
    TextureResource* depthAttachment = nullptr;
    bool depthReadOnly = false;

    std::vector<TextureResource*> storageImageReads;
    std::vector<TextureResource*> storageImageWrites;
    std::vector<TextureResource*> sampledImageReads;
    std::vector<TextureResource*> blitImageReads;
    std::vector<TextureResource*> blitImageWrites;

    std::vector<BufferAccess> bufferReads;
    std::vector<BufferAccess> bufferWrites;
    std::vector<BufferAccess> bufferReadTransfer;
    std::vector<BufferAccess> bufferWriteTransfer;
    std::vector<BufferAccess> bufferIndirectReads;
    std::vector<BufferAccess> bufferIndirectCountReads;

    std::function<void(VkCommandBuffer_T*)> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
