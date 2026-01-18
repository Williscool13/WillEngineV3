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
#include "render/render_config.h"


namespace Render
{
struct ResourceManager;
class RenderPass;
struct TextureResource;
using TransientImageHandle = Core::Handle<TextureResource>;

struct TextureFrameCarryover
{
    std::string srcName;
    std::string dstName;

    VkImage physicalImage;
    TextureInfo textInfo;
    VkImageLayout layout;
    VkImageUsageFlags accumulatedUsage;
};
struct BufferFrameCarryover
{
    std::string srcName;
    std::string dstName;

    VkBuffer buffer;
    BufferInfo bufferInfo;
    VkBufferUsageFlags accumulatedUsage;
};

class RenderGraph
{
public:
    RenderGraph(VulkanContext* context, ResourceManager* resourceManager);

    ~RenderGraph();

    RenderPass& AddPass(const std::string& name, VkPipelineStageFlags2 stages);

    void PrunePasses();

    void AccumulateTextureUsage() const;

    void CalculateLifetimes();

    void Compile(int64_t currentFrame);

    void Execute(VkCommandBuffer cmd);

    void PrepareSwapchain(VkCommandBuffer cmd, const std::string& name);

    void Reset(uint64_t currentFrame, uint64_t maxFramesUnused);

    void SetDebugLogging(bool enable) { bDebugLogging = enable; }

    void InvalidateAll();

    void CreateTexture(const std::string& name, const TextureInfo& texInfo);

    void CreateBuffer(const std::string& name, VkDeviceSize size);

    void ImportTexture(const std::string& name, VkImage image, VkImageView view, const TextureInfo& info, VkImageUsageFlags usage, VkImageLayout initialLayout, VkPipelineStageFlags2 initialStage,
                       VkImageLayout finalLayout);

    void ImportBufferNoBarrier(const std::string& name, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info);

    void ImportBuffer(const std::string& name, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info, PipelineEvent initialState);


    bool HasTexture(const std::string& name);

    bool HasBuffer(const std::string& name);

    VkImage GetImageHandle(const std::string& name);

    VkImageView GetImageViewHandle(const std::string& name);

    VkImageView GetImageViewMipHandle(const std::string& name, uint32_t mipLevel);

    const ResourceDimensions& GetImageDimensions(const std::string& name);

    uint32_t GetSampledImageViewDescriptorIndex(const std::string& name);

    uint32_t GetStorageImageViewDescriptorIndex(const std::string& name, uint32_t mipLevel = 0);

    VkBuffer GetBufferHandle(const std::string& name);

    VkDeviceAddress GetBufferAddress(const std::string& name);

    [[nodiscard]] ResourceManager* GetResourceManager() const { return resourceManager; }

    PipelineEvent GetBufferState(const std::string& name);

    void CarryTextureToNextFrame(const std::string& name, const std::string& newName, VkImageUsageFlags additionalUsage);

    void CarryBufferToNextFrame(const std::string& name, const std::string& newName, VkBufferUsageFlags additionalUsage);

private:
    friend class RenderPass;
    VulkanContext* context;
    ResourceManager* resourceManager;

    // Logical resources
    std::vector<TextureResource> textures;
    std::unordered_map<std::string, uint32_t> textureNameToIndex;

    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_TEXTURES> transientImageHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_TEXTURES> transientStorageImageHandleAllocator;

    std::vector<BufferResource> buffers;
    std::unordered_map<std::string, uint32_t> bufferNameToIndex;

    // Physical resources
    std::vector<PhysicalResource> physicalResources;

    // Render passes
    std::vector<std::unique_ptr<RenderPass> > passes;

    std::vector<TextureFrameCarryover> textureCarryovers;
    std::vector<BufferFrameCarryover> bufferCarryovers;

    bool bDebugLogging = false;

private:
    TextureResource* GetTexture(const std::string& name);

    TextureResource* GetOrCreateTexture(const std::string& name);

    BufferResource* GetBuffer(const std::string& name);

    BufferResource* GetOrCreateBuffer(const std::string& name);

    void DestroyPhysicalResource(PhysicalResource& resource);

    void CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim);

    void CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim);

    void LogImageBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex) const;

    void LogBufferBarrier(const std::string& resourceName, VkAccessFlags2 access) const;

    static void AppendUsageChain(PhysicalResource& phys, const std::string& logicalName, bool canAlias, bool debugLogging)
    {
        if (!debugLogging) return;

        if (phys.usageChain.empty()) {
            phys.usageChain = canAlias ? logicalName : "[noalias]" + logicalName;
        } else {
            phys.usageChain += "->" + logicalName;
        }
    }
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
