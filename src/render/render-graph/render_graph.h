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
struct ResourceManager;
struct TextureResource;
class RenderPass;

class RenderGraph
{
public:
    RenderGraph(VulkanContext* context, ResourceManager* resourceManager);

    RenderPass& AddPass(const std::string& name);

    void Compile();

    void Execute(VkCommandBuffer cmd);

    void Reset();

    uint32_t GetStorageDescriptor();

    uint32_t GetSampledDescriptor(const std::string& name);

    VkImage GetImage(const std::string& name) {
        return textures[textureNameToIndex[name]].image.handle;
    }

    uint32_t GetStorageDescriptorIndex(const std::string& name);

private:
    friend class RenderPass;
    VulkanContext* context;
    ResourceManager* resourceManager;


    std::vector<std::unique_ptr<RenderPass> > passes;

    std::vector<TextureResource> textures;
    std::unordered_map<std::string, uint32_t> textureNameToIndex;

private:
    uint32_t storageDescriptorAllocator = 0;
    uint32_t sampledDescriptorAllocator = 0;

    // Helper for passes to get/create resources
    TextureResource* GetOrCreateTexture(const std::string& name);
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
