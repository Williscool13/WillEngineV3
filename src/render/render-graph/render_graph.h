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

struct FrameCarryover
{
    std::string srcName;
    std::string dstName;

    VkImage physicalImage;
    TextureInfo textInfo;
    VkImageLayout layout;
    VkImageUsageFlags accumulatedUsage;
};

class RenderGraph
{
public:
    RenderGraph(VulkanContext* context, ResourceManager* resourceManager);

    ~RenderGraph();

    RenderPass& AddPass(const std::string& name);

    void PrunePasses();

    void AccumulateTextureUsage() const;

    void CalculateLifetimes();

    void Compile(int64_t currentFrame);

    void Execute(VkCommandBuffer cmd);

    void PrepareSwapchain(VkCommandBuffer cmd, const std::string& name);

    void Reset(uint64_t currentFrame, uint64_t maxFramesUnused);

    void SetDebugLogging(bool enable) { debugLogging = enable; }

    void InvalidateAll();

    void CreateTexture(const std::string& name, const TextureInfo& texInfo);

    void CreateTextureWithUsage(const std::string& name, const TextureInfo& texInfo, VkImageUsageFlags usage);

    void CreateBuffer(const std::string& name, VkDeviceSize size);

    void ImportTexture(const std::string& name, VkImage image, VkImageView view, const TextureInfo& info, VkImageUsageFlags usage, VkImageLayout initialLayout, VkPipelineStageFlags2 initialStage,
                       VkImageLayout finalLayout);

    void ImportBufferNoBarrier(const std::string& name, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info);

    void ImportBuffer(const std::string& name, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info, PipelineEvent initialState);


    bool HasTexture(const std::string& name);

    VkImage GetImage(const std::string& name);

    VkImageView GetImageView(const std::string& name);

    const ResourceDimensions& GetImageDimensions(const std::string& name);

    uint32_t GetDescriptorIndex(const std::string& name);

    VkBuffer GetBuffer(const std::string& name);

    VkDeviceAddress GetBufferAddress(const std::string& name);

    [[nodiscard]] ResourceManager* GetResourceManager() const { return resourceManager; }

    PipelineEvent GetBufferState(const std::string& name);

    void CarryToNextFrame(const std::string& name, const std::string& newName);

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

    // Render passes
    std::vector<std::unique_ptr<RenderPass> > passes;

    std::vector<FrameCarryover> carryovers;

    bool debugLogging = false;

private:
    TextureResource* GetTexture(const std::string& name);

    TextureResource* GetOrCreateTexture(const std::string& name);

    BufferResource* GetOrCreateBuffer(const std::string& name);

    void DestroyPhysicalResource(PhysicalResource& resource);

    void CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim);

    void CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim);

    void LogImageBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex) const;

    void LogBufferBarrier(const std::string& resourceName, VkAccessFlags2 access) const;
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
