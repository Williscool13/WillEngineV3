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

    RenderPass& WriteBlitImage(const std::string& name);

    RenderPass& WriteColorAttachment(const std::string& name, const TextureInfo& texInfo = {});

    RenderPass& WriteDepthAttachment(const std::string& name, const TextureInfo& texInfo = {});


    // Read
    RenderPass& ReadDepthAttachment(const std::string& name);

    RenderPass& ReadStorageImage(const std::string& name);

    RenderPass& ReadSampledImage(const std::string& name);

    RenderPass& ReadBlitImage(const std::string& name);

    RenderPass& ReadBuffer(const std::string& name, VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);

    RenderPass& Blit(const std::string& src, const std::string& dst, VkFilter filter);

    RenderPass& Execute(std::function<void(VkCommandBuffer)> func);

private:
    friend class RenderGraph;
    RenderGraph& graph;
    std::string renderPassName;

    struct BufferRead
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
    std::vector<BufferRead> bufferReads;

    struct BlitOp {
        std::string src;
        std::string dst;
        VkFilter filter;
    };
    std::vector<BlitOp> blitOps;
    std::function<void(VkCommandBuffer_T*)> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H
