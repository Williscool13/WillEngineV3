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

    ~RenderGraph();

    RenderPass& AddPass(const std::string& name);

    void Compile();

    void Execute(VkCommandBuffer cmd);

    void Reset();

    void SetDebugLogging(bool enable) { debugLogging = enable; }

    VkImage GetImage(const std::string& name);

    uint32_t GetDescriptorIndex(const std::string& name);

    void ImportTexture(const std::string& name, VkImage image, VkImageView view, VkImageLayout initialLayout, VkPipelineStageFlags2 initialStage, VkImageLayout finalLayout);

    PipelineEvent GetResourceState(const std::string& name) const;

private:
    friend class RenderPass;
    VulkanContext* context;
    ResourceManager* resourceManager;

    // Logical resources
    std::vector<TextureResource> textures;
    std::unordered_map<std::string, uint32_t> textureNameToIndex;
    static constexpr uint32_t MAX_TEXTURES = 128;

    // Physical resources
    std::vector<PhysicalResource> physicalResources;

    // Render passes
    std::vector<std::unique_ptr<RenderPass> > passes;

    bool debugLogging = false;

private:
    TextureResource* GetOrCreateTexture(const std::string& name);

    void DestroyPhysicalResource(PhysicalResource& resource);

    void CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim);

    void CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim);

    void LogBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName);
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
