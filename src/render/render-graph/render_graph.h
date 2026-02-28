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
    StringID srcName;
    StringID dstName;

    VkImage physicalImage;
    TextureInfo textInfo;
    VkImageLayout layout;
    VkImageUsageFlags accumulatedUsage;
};

struct BufferFrameCarryover
{
    StringID srcName;
    StringID dstName;

    VkBuffer buffer;
    BufferInfo bufferInfo;
    VkBufferUsageFlags accumulatedUsage;
};

class RenderGraph
{
public:
    RenderGraph(VulkanContext* context, ResourceManager* resourceManager);

    ~RenderGraph();

    RenderPass& AddPass(StringID passId, VkPipelineStageFlags2 stages);

    void PrunePasses();

    void AccumulateTextureUsage();

    void CalculateLifetimes();

    void Compile(int64_t currentFrame);

    void Execute(VkCommandBuffer cmd);

    void PrepareSwapchain(VkCommandBuffer cmd, StringID textureId);

    void Reset(uint32_t _currentFrameIndex, uint64_t currentFrame, uint64_t maxFramesUnused);

    void SetDebugLogging(bool enable) { bDebugLogging = enable; }

    void InvalidateAllViewportAssociated() { bDestroyViewportAssociated = true; }

    void CreateTexture(StringID textureId, const TextureInfo& texInfo, bool bIsViewportScaled = false);

    void AliasTexture(StringID aliasId, StringID existingId);

    void CreateBuffer(StringID bufferId, VkDeviceSize size, bool bIsViewportScaled = false, bool bCanAlias = true);

    void ImportTexture(StringID textureId, VkImage image, VkImageView view, const TextureInfo& info, VkImageUsageFlags usage, VkImageLayout initialLayout, VkPipelineStageFlags2 initialStage,
                       VkImageLayout finalLayout);

    void ImportBufferNoBarrier(StringID bufferId, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info);

    void ImportBuffer(StringID bufferId, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info, PipelineEvent initialState);

    bool HasTexture(StringID textureId);

    bool HasBuffer(StringID bufferId);

    VkImage GetImageHandle(StringID textureId);

    VkImageView GetImageViewHandle(StringID textureId);

    VkImageView GetImageViewMipHandle(StringID textureId, uint32_t mipLevel);

    VkImageView GetDepthOnlyImageViewHandle(StringID textureId);

    VkImageView GetStencilOnlyImageViewHandle(StringID textureId);

    const ResourceDimensions& GetImageDimensions(StringID textureId);

    const VkImageAspectFlags GetImageAspect(StringID textureId);

    uint32_t GetSampledImageViewDescriptorIndex(StringID textureId);

    uint32_t GetStorageImageViewDescriptorIndex(StringID textureId, uint32_t mipLevel = 0);

    uint32_t GetDepthOnlySampledImageViewDescriptorIndex(StringID textureId);

    uint32_t GetStencilOnlyStorageImageViewDescriptorIndex(StringID textureId);

    VkBuffer GetBufferHandle(StringID bufferId);

    VkDeviceAddress GetBufferAddress(StringID bufferId);

    [[nodiscard]] ResourceManager* GetResourceManager() const { return resourceManager; }

    PipelineEvent GetBufferState(StringID bufferId);

    void CarryTextureToNextFrame(StringID textureId, StringID newTextureId, VkImageUsageFlags additionalUsage);

    void CarryBufferToNextFrame(StringID bufferId, StringID newBufferId, VkBufferUsageFlags additionalUsage);

public: // Transient Uploader
    UploadAllocation AllocateTransient(size_t size);

    VkBuffer GetTransientUploadBuffer() const { return uploadArenas[currentFrameIndex].buffer.handle; }

public: // Readback
    VkBuffer GetReadback() const { return meshletCountReadbacks[currentFrameIndex].buffer.handle; }

    ReadbackStruct* GetReadbackData() const { return static_cast<ReadbackStruct*>(meshletCountReadbacks[currentFrameIndex].buffer.allocationInfo.pMappedData); }

private:
    friend class RenderPass;
    VulkanContext* context;
    ResourceManager* resourceManager;

    // Logical resources
    std::vector<TextureResource> textures;
    std::unordered_map<StringID, uint32_t> textureNameToIndex;

    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_TEXTURES> transientSampledImageHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT4> transientStorageFloat4HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT2> transientStorageFloat2HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT> transientStorageFloatHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_UINT4> transientStorageUInt4HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_UINT> transientStorageUIntHandleAllocator;

    std::vector<BufferResource> buffers;
    std::unordered_map<StringID, uint32_t> bufferNameToIndex;

    // Physical resources
    std::vector<PhysicalResource> physicalResources;

    // Render passes
    std::vector<std::unique_ptr<RenderPass> > passes;

    std::vector<TextureFrameCarryover> textureCarryovers;
    std::vector<BufferFrameCarryover> bufferCarryovers;

    uint32_t currentFrameIndex{0};
    std::array<TransientUploadArena, Core::FRAME_BUFFER_COUNT> uploadArenas{};
    std::array<TransientReadback, Core::FRAME_BUFFER_COUNT> meshletCountReadbacks{};

    bool bDestroyViewportAssociated{false};

    bool bDebugLogging = false;
    uint32_t debugNameCounter{0};

private:
    TextureResource* GetTexture(StringID imageId);

    TextureResource* GetOrCreateTexture(StringID textureId);

    BufferResource* GetBuffer(StringID bufferId);

    BufferResource* GetOrCreateBuffer(StringID textureId);

    void DestroyPhysicalResource(PhysicalResource& resource);

    void CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim);

    void CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim);

    void RecreateTransientArena(uint32_t frameIndex, size_t newSize);

    void LogImageBarrier(StringID textureId, const VkImageMemoryBarrier2& barrier, uint32_t physicalIndex) const;

    void LogBufferBarrier(StringID bufferId, VkAccessFlags2 access) const;

    static void AppendUsageChain(PhysicalResource& phys, StringID resourceId, bool canAlias, bool debugLogging);
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
