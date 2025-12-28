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
#include "render/vulkan/vk_resources.h"
#include "core/allocators/handle_allocator.h"

namespace Render
{
struct ResourceManager;
class RenderPass;
struct TextureResource;
using TransientImageHandle = Core::Handle<TextureResource>;

class RenderGraph
{
public:
    RenderGraph(VulkanContext* context, ResourceManager* resourceManager);

    ~RenderGraph();

    RenderPass& AddPass(const std::string& name);

    void AccumulateTextureUsage() const;

    void CalculateLifetimes();

    void Compile();

    void Execute(VkCommandBuffer cmd);

    void Reset();

    void SetDebugLogging(bool enable) { debugLogging = enable; }

    void ImportTexture(const std::string& name, VkImage image, VkImageView view, const TextureInfo& info, VkImageUsageFlags usage, VkImageLayout initialLayout, VkPipelineStageFlags2 initialStage, VkImageLayout finalLayout);

    void ImportBuffer(const std::string& name, VkBuffer buffer, const BufferInfo& info, VkPipelineStageFlags2 initialStage);


    VkImage GetImage(const std::string& name);

    uint32_t GetDescriptorIndex(const std::string& name);

    VkImageView GetImageView(const std::string& name);

    VkDeviceAddress GetBufferAddress(const std::string& name);

    [[nodiscard]] PipelineEvent GetResourceState(const std::string& name) const;

    [[nodiscard]] ResourceManager* GetResourceManager() const { return resourceManager;}

private:
    friend class RenderPass;
    VulkanContext* context;
    ResourceManager* resourceManager;

    // Logical resources
    std::vector<TextureResource> textures;
    std::unordered_map<std::string, uint32_t> textureNameToIndex;
    static constexpr uint32_t MAX_TEXTURES = 128;
    Core::HandleAllocator<TextureResource, MAX_TEXTURES> transientImageHandleAllocator;

    std::vector<BufferResource> buffers;
    std::unordered_map<std::string, uint32_t> bufferNameToIndex;

    // Physical resources
    std::vector<PhysicalResource> physicalResources;

    struct ImportedImageInfo
    {
        uint32_t physicalIndex;
        uint32_t lifetime;
    };

    std::unordered_map<VkImage, ImportedImageInfo> importedImages;

    struct ImportedBufferInfo
    {
        uint32_t physicalIndex;
        uint32_t lifetime;
    };

    std::unordered_map<VkBuffer, ImportedBufferInfo> importedBuffers;

    // Render passes
    std::vector<std::unique_ptr<RenderPass> > passes;

    bool debugLogging = false;

private:
    TextureResource* GetOrCreateTexture(const std::string& name);

    BufferResource* GetOrCreateBuffer(const std::string& name);

    void DestroyPhysicalResource(PhysicalResource& resource);

    void CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim);

    void CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim);

    void LogBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex) const;
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
