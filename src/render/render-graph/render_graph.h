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
#include "core/include/render_interface.h"
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

    void AccumulateTextureUsage();

    void CalculateLifetimes();

    void Compile(int64_t currentFrame);

    void Execute(VkCommandBuffer cmd);

    void PrepareSwapchain(VkCommandBuffer cmd, const std::string& name);

    void Reset(uint32_t _currentFrameIndex, uint64_t currentFrame, uint64_t maxFramesUnused);

    void SetDebugLogging(bool enable) { bDebugLogging = enable; }

    void InvalidateAll();

    void CreateTexture(const std::string& name, const TextureInfo& texInfo);

    void AliasTexture(const std::string& aliasName, const std::string& existingName);

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

    VkImageView GetDepthOnlyImageViewHandle(const std::string& name);

    VkImageView GetStencilOnlyImageViewHandle(const std::string& name);

    const ResourceDimensions& GetImageDimensions(const std::string& name);

    const VkImageAspectFlags GetImageAspect(const std::string& name);

    uint32_t GetSampledImageViewDescriptorIndex(const std::string& name);

    uint32_t GetStorageImageViewDescriptorIndex(const std::string& name, uint32_t mipLevel = 0);

    uint32_t GetDepthOnlySampledImageViewDescriptorIndex(const std::string& name);

    uint32_t GetStencilOnlyStorageImageViewDescriptorIndex(const std::string& name);

    VkBuffer GetBufferHandle(const std::string& name);

    VkDeviceAddress GetBufferAddress(const std::string& name);

    [[nodiscard]] ResourceManager* GetResourceManager() const { return resourceManager; }

    PipelineEvent GetBufferState(const std::string& name);

    void CarryTextureToNextFrame(const std::string& name, const std::string& newName, VkImageUsageFlags additionalUsage);

    void CarryBufferToNextFrame(const std::string& name, const std::string& newName, VkBufferUsageFlags additionalUsage);

public: // Transient Uploader
    UploadAllocation AllocateTransient(size_t size);

    VkBuffer GetTransientUploadBuffer() const { return uploadArenas[currentFrameIndex].buffer.handle; }

private:
    friend class RenderPass;
    VulkanContext* context;
    ResourceManager* resourceManager;

    // Logical resources
    std::vector<TextureResource> textures;
    std::unordered_map<std::string, uint32_t> textureNameToIndex;

    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_TEXTURES> transientSampledImageHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT4> transientStorageFloat4HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT2> transientStorageFloat2HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT> transientStorageFloatHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_UINT4> transientStorageUInt4HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_UINT> transientStorageUIntHandleAllocator;

    std::vector<BufferResource> buffers;
    std::unordered_map<std::string, uint32_t> bufferNameToIndex;

    // Physical resources
    std::vector<PhysicalResource> physicalResources;

    // Render passes
    std::vector<std::unique_ptr<RenderPass> > passes;

    std::vector<TextureFrameCarryover> textureCarryovers;
    std::vector<BufferFrameCarryover> bufferCarryovers;

    uint32_t currentFrameIndex{0};
    std::array<TransientUploadArena, Core::FRAME_BUFFER_COUNT> uploadArenas{};

    bool bDebugLogging = false;
    uint32_t debugNameCounter{0};

private:
    TextureResource* GetTexture(const std::string& name);

    TextureResource* GetOrCreateTexture(const std::string& name);

    BufferResource* GetBuffer(const std::string& name);

    BufferResource* GetOrCreateBuffer(const std::string& name);

    void DestroyPhysicalResource(PhysicalResource& resource);

    void CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim);

    void CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim);

    void RecreateTransientArena(uint32_t frameIndex, size_t newSize);

    void LogImageBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex) const;

    void LogBufferBarrier(const std::string& resourceName, VkAccessFlags2 access) const;

    static void AppendUsageChain(PhysicalResource& phys, const std::string& logicalName, bool canAlias, bool debugLogging)
    {
        if (!debugLogging) return;

        if (phys.usageChain.empty()) {
            phys.usageChain = canAlias ? logicalName : "[noalias]" + logicalName;
        }
        else {
            phys.usageChain += "->" + logicalName;
        }
    }
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
