//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_H
#define WILL_ENGINE_RENDER_GRAPH_H

#include "core/containers/arena_fixed_map.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/array.h"
#include "core/containers/vector.h"
#include "core/memory/arena.h"
#include "core/memory/handle_allocator.h"
#include "render/interface/render_interface.h"
#include "render/vulkan/vk_resources.h"
#include "render/render_config.h"
#include "render_graph_resources.h"

namespace Render
{
struct ResourceManager;
class RenderPass;
struct TextureResource;
using TransientImageHandle = Core::Handle<TextureResource>;

class RenderGraph
{
public:
    RenderGraph(VulkanContext* context, ResourceManager* resourceManager, Core::TlsfAllocator& alloc, Core::Arena& arena);

    ~RenderGraph();

public: // Frame setup
    /**
     *
     * @param _currentFrameIndex
     * @param currentFrame
     * @param maxFramesUnused physical resources unused for this many frames are evicted
     */
    void Reset(uint32_t _currentFrameIndex, uint64_t currentFrame, uint64_t maxFramesUnused);

    void SetDebugLogging(bool enable) { bDebugLogging = enable; }

    /**
     * Destroys all viewport-scaled physical resources so they are recreated at the new size next frame
     */
    void InvalidateAllViewportAssociated() { bDestroyViewportAssociated = true; }

    void InvalidateAllSwapchainAssociated() { bRemoveSwapchainPhysicals = true; }

public: // Resource registration
    void CreateTexture(StringID textureId, const TextureInfo& texInfo, std::optional<VkClearValue> clearValue = std::nullopt, bool bIsViewportScaled = false);

    /**
     * Makes aliasId refer to the same logical resource as existingId
     * @param aliasId
     * @param existingId
     */
    void AliasTexture(StringID aliasId, StringID existingId);

    void CreateBuffer(StringID bufferId, VkDeviceSize size, bool bIsViewportScaled = false, bool bCanAlias = true);

    void ImportTexture(StringID textureId, VkImage image, VkImageView view, const TextureInfo& info, VkImageUsageFlags usage, VkImageLayout initialLayout, VkPipelineStageFlags2 initialStage,
                       VkImageLayout finalLayout, bool bIsSwapchain = false);


    /**
     * Imports a buffer with no barrier tracking; the caller is responsible for synchronization.
     * @param bufferId
     * @param buffer
     * @param address
     * @param info
     */
    void ImportBufferNoBarrier(StringID bufferId, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info);

    void ImportBuffer(StringID bufferId, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info, PipelineEvent initialState);

    /**
     * Carries a texture across a frame boundary; the physical image is re-imported under newTextureId next frame
     * @param textureId
     * @param newTextureId
     * @param additionalUsage
     */
    void CarryTextureToNextFrame(StringID textureId, StringID newTextureId, VkImageUsageFlags additionalUsage);

    void CarryBufferToNextFrame(StringID bufferId, StringID newBufferId, VkBufferUsageFlags additionalUsage);

public: // Pass setup
    RenderPass& AddPass(StringID passId, VkPipelineStageFlags2 stages);

public: // Resource queries
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

public: // Compile and execute
    /**
     * Removes passes with no execute callback and no side effects
     */
    void PrunePasses();

    /**
     * Accumulates VkImageUsageFlags / VkBufferUsageFlags across all passes for physical resource creation
     */
    void AccumulateUsage();

    void BuildDependencyEdges();

    void TopologicalSortPasses();

    void AssignWaveIndices();

    /**
     * Computes firstPass/lastPass for each logical resource to drive physical resource aliasing
     */
    void CalculateLifetimes();

    void PopulateAutoClearTextures();

    /**
     * Allocates/aliases physical resources and writes descriptors; call after CalculateLifetimes
     * @param currentFrame
     */
    void Compile(int64_t currentFrame);

    void Execute(VkCommandBuffer cmd);

    /**
     * Transitions the named texture to present-src layout; call after Execute
     * @param cmd
     * @param textureId
     */
    void PrepareSwapchain(VkCommandBuffer cmd, StringID textureId);

public: // Transient Uploader
    UploadAllocation AllocateTransient(size_t size);

    [[nodiscard]] VkBuffer GetTransientUploadBuffer() const { return uploadArenas[currentFrameIndex].buffer.handle; }

public: // Readback
    [[nodiscard]] VkBuffer GetReadback() const { return meshletCountReadbacks[currentFrameIndex].buffer.handle; }

    [[nodiscard]] ReadbackStruct* GetReadbackData() const { return static_cast<ReadbackStruct*>(meshletCountReadbacks[currentFrameIndex].buffer.allocationInfo.pMappedData); }

private:
    friend class RenderPass;
    VulkanContext* context;
    ResourceManager* resourceManager;
    Core::TlsfAllocator* alloc;
    Core::Arena* arena;

    // Logical resources
    Core::ArenaFixedVector<TextureResource> textures;
    Core::ArenaFixedMap<StringID, uint32_t> textureNameToIndex;

    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_TEXTURES> transientSampledImageHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT4> transientStorageFloat4HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT2> transientStorageFloat2HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_FLOAT> transientStorageFloatHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_UINT4> transientStorageUInt4HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_UINT2> transientStorageUInt2HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_STORAGE_UINT> transientStorageUIntHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_FLOAT2> transientSampledFloat2HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_FLOAT> transientSampledFloatHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_UINT4> transientSampledUInt4HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_UINT2> transientSampledUInt2HandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_SAMPLED_UINT> transientSampledUIntHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_MULTISAMPLED_IMAGE> transientMultisampledImageHandleAllocator;
    Core::HandleAllocator<TextureResource, RDG_MAX_MULTISAMPLED_UINT_IMAGE> transientMultisampledUIntImageHandleAllocator;

    Core::ArenaFixedVector<BufferResource> buffers;
    Core::ArenaFixedMap<StringID, uint32_t> bufferNameToIndex;

    // Physical resources
    Core::Vector<PhysicalResource> physicalResources;

    // Render passes
    Core::ArenaFixedVector<RenderPass*> passes;
    // Generated at compile time
    Core::ArenaFixedVector<RenderPass*> sortedPasses;
    Core::Vector<uint32_t> waveOffsets;

    Core::Vector<TextureFrameCarryover> textureCarryovers;
    Core::Vector<BufferFrameCarryover> bufferCarryovers;

    uint32_t currentFrameIndex{0};
    Core::Array<TransientUploadArena, Core::FRAME_BUFFER_COUNT> uploadArenas{};
    Core::Array<TransientReadback, Core::FRAME_BUFFER_COUNT> meshletCountReadbacks{};

    bool bRemoveSwapchainPhysicals{false};
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

    void ExportGraphviz();

    static void AppendUsageChain(PhysicalResource& phys, StringID resourceId, bool bCanAlias, bool debugLogging);
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_H
