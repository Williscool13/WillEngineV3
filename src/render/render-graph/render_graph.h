//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_H
#define WILL_ENGINE_RENDER_GRAPH_H
#include <functional>
#include <memory>
#include <string>

#include <volk.h>

#include "render_graph_resources.h"
#include "../vulkan/vk_resources.h"

namespace Render
{
struct TextureResource;
class RenderPass;

class RenderGraph
{
public:
    RenderPass& AddPass(const std::string& name);

    void Compile();

    void Execute(VkCommandBuffer cmd);

    void Reset();

    uint32_t GetDescriptor(const std::string& name);

private:
    friend class RenderPass;
    std::vector<std::unique_ptr<RenderPass> > passes;

    std::vector<TextureResource> textures;
    std::unordered_map<std::string, uint32_t> textureNameToIndex;

    // Helper for passes to get/create resources
    TextureResource* GetOrCreateTexture(const std::string& name);
};

class RenderPass
{
public:
    RenderPass(RenderGraph& renderGraph, std::string name);

    RenderPass& WriteStorageImage(const std::string& name, TextureInfo info = {});

    RenderPass& Execute(std::function<void(VkCommandBuffer)> func);

private:
    RenderGraph& graph;
    std::string renderPassName;

    std::vector<TextureResource*> writtenStorageImages;
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
