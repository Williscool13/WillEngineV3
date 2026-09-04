//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_H
#define WILL_ENGINE_RENDER_GRAPH_H

#include "core/containers/arena_fixed_map.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/array.h"
#include "core/containers/inline_function.h"
#include "core/containers/inline_vector.h"
#include "core/containers/vector.h"
#include "core/memory/arena.h"
#include "core/memory/handle_allocator.h"
#include "render/interface/render_interface.h"
#include "render/vulkan/vk_resources.h"
#include "render/render_config.h"
#include "render/vulkan/vk_gpu_timestamp_query.h"
#include "render_graph_resources.h"

namespace Render
{
struct ResourceManager;
class RenderPass;
struct TextureResource;
using TransientImageHandle = Core::Handle<TextureResource>;

struct RenderGraphAllocFns
{
    struct ImageAlloc { VkImage image; VmaAllocation allocation; };
    struct BufferAlloc { VkBuffer buffer; VmaAllocation allocation; void* mappedData; };

    static ImageAlloc DefaultCreateImage(const VulkanContext*, const VkImageCreateInfo&);
    static VkImageView DefaultCreateImageView(const VulkanContext*, const VkImageViewCreateInfo&);
    static void DefaultDestroyImage(const VulkanContext*, VkImage, VmaAllocation);
    static void DefaultDestroyImageView(const VulkanContext*, VkImageView);
    static BufferAlloc DefaultCreateBuffer(const VulkanContext*, const VkBufferCreateInfo&, const VmaAllocationCreateInfo&);
    static BufferAlloc DefaultCreateBufferAligned(const VulkanContext*, const VkBufferCreateInfo&, const VmaAllocationCreateInfo&, VkDeviceSize minAlignment);
    static void DefaultDestroyBuffer(const VulkanContext*, VkBuffer, VmaAllocation);
    static void DefaultDestroyAccelerationStructure(const VulkanContext*, VkAccelerationStructureKHR);
    static VkDeviceAddress DefaultGetBufferDeviceAddress(const VulkanContext*, VkBuffer);
    static void DefaultSetDebugName(const VulkanContext*, VkObjectType, uint64_t handle, const char* name);
    static void DefaultCmdPipelineBarrier2(VkCommandBuffer, const VkDependencyInfo*);
    static void DefaultCmdClearColorImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue*, uint32_t, const VkImageSubresourceRange*);
    static void DefaultCmdClearDepthStencilImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue*, uint32_t, const VkImageSubresourceRange*);
    static void DefaultCmdBeginDebugUtilsLabel(VkCommandBuffer, const VkDebugUtilsLabelEXT*);
    static void DefaultCmdEndDebugUtilsLabel(VkCommandBuffer);

    Core::InlineFunction<ImageAlloc(const VulkanContext*, const VkImageCreateInfo&), 64> createImage{DefaultCreateImage};
    Core::InlineFunction<VkImageView(const VulkanContext*, const VkImageViewCreateInfo&), 64> createImageView{DefaultCreateImageView};
    Core::InlineFunction<void(const VulkanContext*, VkImage, VmaAllocation), 64> destroyImage{DefaultDestroyImage};
    Core::InlineFunction<void(const VulkanContext*, VkImageView), 64> destroyImageView{DefaultDestroyImageView};
    Core::InlineFunction<BufferAlloc(const VulkanContext*, const VkBufferCreateInfo&, const VmaAllocationCreateInfo&), 64> createBuffer{DefaultCreateBuffer};
    Core::InlineFunction<BufferAlloc(const VulkanContext*, const VkBufferCreateInfo&, const VmaAllocationCreateInfo&, VkDeviceSize), 64> createBufferAligned{DefaultCreateBufferAligned};
    Core::InlineFunction<void(const VulkanContext*, VkBuffer, VmaAllocation), 64> destroyBuffer{DefaultDestroyBuffer};
    Core::InlineFunction<void(const VulkanContext*, VkAccelerationStructureKHR), 64> destroyAccelerationStructure{DefaultDestroyAccelerationStructure};
    Core::InlineFunction<VkDeviceAddress(const VulkanContext*, VkBuffer), 64> getBufferDeviceAddress{DefaultGetBufferDeviceAddress};
    Core::InlineFunction<void(const VulkanContext*, VkObjectType, uint64_t, const char*), 64> setDebugName{DefaultSetDebugName};
    Core::InlineFunction<void(VkCommandBuffer, const VkDependencyInfo*), 64> cmdPipelineBarrier2{DefaultCmdPipelineBarrier2};
    Core::InlineFunction<void(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue*, uint32_t, const VkImageSubresourceRange*), 64> cmdClearColorImage{DefaultCmdClearColorImage};
    Core::InlineFunction<void(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue*, uint32_t, const VkImageSubresourceRange*), 64> cmdClearDepthStencilImage{DefaultCmdClearDepthStencilImage};
    Core::InlineFunction<void(VkCommandBuffer, const VkDebugUtilsLabelEXT*), 64> cmdBeginDebugUtilsLabel{DefaultCmdBeginDebugUtilsLabel};
    Core::InlineFunction<void(VkCommandBuffer), 64> cmdEndDebugUtilsLabel{DefaultCmdEndDebugUtilsLabel};
    // Optional: when set, replaces the entire NeedsDescriptorWrite block for a physical resource. Tests set this to a no-op.
    Core::InlineFunction<void(PhysicalResource&), 64> writeDescriptors;
};

class RenderGraph
{
public:
    RenderGraph(VulkanContext* context, ResourceManager* resourceManager, Core::TlsfAllocator& alloc, Core::Arena& arena, RenderGraphAllocFns allocFns = {});

    ~RenderGraph();

public: // Frame setup
    /**
     *
     * @param _currentFrameIndex
     * @param currentFrame
     * @param maxFramesUnused physical resources unused for this many frames are evicted
     */
    void Reset(uint32_t _currentFrameIndex, uint64_t currentFrame, uint64_t maxFramesUnused);

    void SetDebugLogging(bool enable)
    {
        if (enable && !bDebugLogging) {
            bDebugLogging = true;
            debugCaptureFramesLeft = 2;
        }
    }

    /**
     * Destroys all viewport-scaled physical resources so they are recreated at the new size next frame
     */
    void InvalidateAllViewportAssociated() { bDestroyViewportAssociated = true; }

    /**
     * Drops every versioned resource (textures, buffers, the TLAS ring) regardless of viewport flag; physicals age out normally
     */
    void InvalidateAllVersioned() { bDropAllRings = true; }

    void InvalidateAllSwapchainAssociated() { bRemoveSwapchainPhysicals = true; }

public: // Resource registration
    void CreateTexture(StringID textureId, const TextureInfo& texInfo, std::optional<VkClearValue> clearValue = std::nullopt, bool bIsViewportScaled = false);

    /**
     * Makes aliasId refer to the same logical resource as existingId
     * @param aliasId
     * @param existingId
     */
    void AliasTexture(StringID aliasId, StringID existingId);

    void AliasBuffer(StringID aliasId, StringID existingId);

    void CreateBuffer(StringID bufferId, VkDeviceSize size, bool bIsViewportScaled = false, bool bCanAlias = true);
    void CreateBufferAligned(StringID bufferId, VkDeviceSize size, VkDeviceSize minAlignment, bool bIsViewportScaled = false, bool bCanAlias = true);

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

public: // Rings
    /**
     * Declares a texture the graph keeps across frames. The bare name is the newest version, ResourceVersionID(name, age) the one produced age versions earlier (1..depth).
     * A ring not declared for a frame is dropped; a changed description resets it.
     * @param name
     * @param texInfo
     * @param depth 0..RDG_MAX_RING_DEPTH; 0 is the in-place case (Fresh once, then NoShiftReadWrite: one physical for life)
     * @param source see VersionSource
     * @param bIsViewportScaled
     * @param extraUsage usage the versions need beyond what this frame's passes declare on whatever produces them (a later consumer's sampled read, a copy source)
     * @param bConcurrent an async pass touches some version of this ring; every physical is created CONCURRENT and pinned GENERAL. Asserted if an undeclared ring is touched by async.
     * @param clearValue auto-clear for the bare name on frames it is produced
     */
    void CreateVersionedTexture(StringID name, const TextureInfo& texInfo, uint32_t depth, VersionSource source, bool bIsViewportScaled = false, VkImageUsageFlags extraUsage = 0, bool bConcurrent = false, std::optional<VkClearValue> clearValue = std::nullopt);

    void CreateVersionedBuffer(StringID name, VkDeviceSize size, uint32_t depth, VersionSource source, VkDeviceSize minAlignment = 0, VkBufferUsageFlags extraUsage = 0);

    void CreateVersionedTLAS(StringID name, VkDeviceSize asSize, RenderCategory category = RenderCategory::Untagged);

    /**
     * Supplies this frame's version of a ring declared VersionSource::Emplaced from an existing logical's physical.
     * The ring takes the source's description, so the next declaration must match it or the history resets.
     * @param resourceDst
     * @param resourceSrc
     */
    void EmplaceVersion(StringID resourceDst, StringID resourceSrc);

    /** @returns the logical name of the version from age frames ago; age 0 is the bare name. Pure naming, no lookup: the version need not exist (see HasVersion) */
    [[nodiscard]] StringID ResourceVersionID(StringID name, uint32_t age);

    /** @returns true when that version holds a produced physical. Age 0 is pending on a shifting source until frame end, so it answers "was anything produced before" on the no-shift sources */
    [[nodiscard]] bool ResourceHasVersion(StringID name, uint32_t age);

public: // Pass setup
    RenderPass& AddPass(StringID passId, VkPipelineStageFlags2 stages, RenderCategory category);

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

    /**
     * Capture variants for passes that bake handles into a bundle for downstream consumers without accessing the resource themselves (No usage/aliasing validation).
     */
    uint32_t PeekSampledImageViewDescriptorIndex(StringID textureId);

    VkDeviceAddress PeekBufferAddress(StringID bufferId);

    uint32_t GetDepthOnlySampledImageViewDescriptorIndex(StringID textureId);

    uint32_t GetStencilOnlyStorageImageViewDescriptorIndex(StringID textureId);

    VkBuffer GetBufferHandle(StringID bufferId);

    VkDeviceAddress GetBufferAddress(StringID bufferId);
    VkDeviceAddress TryGetBufferAddress(StringID bufferId);

    [[nodiscard]] ResourceManager* GetResourceManager() const { return resourceManager; }

    PipelineEvent GetBufferState(StringID bufferId);

    /**
     * If true render graph will not execute and the application will be requested to shut down.
     * @return
     */
    [[nodiscard]] bool IsFrameCorrupted() const { return bFrameCorrupted; }

public: // VRAM reporting
    VRAMReport GenerateVramReport() const;

public: // Compile and execute
    /**
     * Accumulates VkImageUsageFlags / VkBufferUsageFlags across all passes for physical resource creation
     */
    void AccumulateUsage();

    void BuildDependencyEdges();

    /**
     * Pulls graph-synthesized upload passes into the async cut of their consumer and rejects any async pass that depends on a graphics pass.
     */
    void PropagateAsyncPasses();

    void TopologicalSortPasses();

    void AssignWaveIndices();

    /**
     * Computes firstPass/lastPass for each logical resource to drive physical resource aliasing
     */
    void CalculateLifetimes();

    void PopulateAutoClearTextures();

    void AssignPhysicalResources(uint64_t currentFrame);

    /**
     * Every async pass declaration must touch memory graphics cannot still be using: writes need FRAME_BUFFER_COUNT frames since any graphics touch, reads since a graphics write, unless an async pass wrote it this frame or last touched it.
     */
    void ValidateAsyncHazards(uint64_t currentFrame);

    /**
     * Precomputes per-wave and per-pass barriers into flat arrays; call after Compile
     */
    void PrecomputeBarriers(uint64_t currentFrame);

    /**
     * Allocates/aliases physical resources and writes descriptors; call after CalculateLifetimes
     * @param currentFrame
     */
    void Compile(uint64_t currentFrame);

    void Execute(VkCommandBuffer asyncCmd, VkCommandBuffer cmd);

    /** Accumulated dst stages of the suppressed async->graphics barriers */
    [[nodiscard]] VkPipelineStageFlags2 GetCrossCutWaitStageMask() const { return crossCutWaitStageMask; }

    /**
     * Transitions the named texture to present-src layout; call after Execute
     * @param cmd
     * @param textureId
     */
    void PrepareSwapchain(VkCommandBuffer cmd, StringID textureId);

public: // Persistent Per-FIF Buffers
    /**
     * Registers the buffer on first use, grows it to size, imports the current frame's slot so passes can declare reads against the name, and returns memory to write into.
     * On a device without resizable BAR the returned memory is staging and a copy into the slot is queued automatically, so callers never branch.
     * @param name
     * @param size
     * @param extraUsage usage beyond storage/device address/transfer, needed because a persistent slot cannot pick usage up from pass declarations
     */
    void* OpenHostBuffer(StringID name, VkDeviceSize size, VkBufferUsageFlags extraUsage = 0);

    /**
     * OpenHostBuffer for a caller that writes only the parts that changed. Returns every destination the write has to reach, so the buffer survives a reallocation and works without REBAR.
     * Only use it for a buffer whose unwritten bytes must persist: a buffer rewritten in full every frame must stay on OpenHostBuffer, or it pays for a mirror that only goes stale.
     */
    HostBufferWrite OpenHostBufferMirrored(StringID name, VkDeviceSize size, VkBufferUsageFlags extraUsage = 0);

    VkAccelerationStructureKHR GetAccelerationStructureHandle(StringID name);

    uint32_t GetAccelerationStructureDescriptorIndex(StringID name);

public: // Transient Uploader
    UploadAllocation AllocateTransient(size_t size);

    [[nodiscard]] VkBuffer GetTransientUploadBuffer() const { return uploadArenas[currentFrameIndex].buffer; }

    [[nodiscard]] void* GetTransientUploadMapped() const { return uploadArenas[currentFrameIndex].mappedData; }

public: // Readback
    [[nodiscard]] VkBuffer GetReadback() const { return meshletCountReadbacks[currentFrameIndex].buffer; }

    [[nodiscard]] ReadbackStruct* GetReadbackData() const { return static_cast<ReadbackStruct*>(meshletCountReadbacks[currentFrameIndex].mappedData); }

public: // GPU pass timing
    /** Reads the previous use of this frame-in-flight slot; call once per frame before Execute (mirrors PipelineStatsQueryPool::Collect). */
    GPUProfileSnapshot CollectGPUProfile(uint32_t frameIndex);

private:
    friend class RenderPass;
    friend class RenderGraphInspector;

    void ValidatePassDeclaresTexture(uint32_t textureIndex);
    void ValidatePassDeclaresBuffer(uint32_t bufferIndex);

    /** Queues a replaced buffer for destruction once no frame in flight can still reference it. */
    void RetireBuffer(VkBuffer buffer, VmaAllocation allocation);

    void RegisterHostBuffer(StringID name, VkBufferUsageFlags usage);

    /** @return true if the slot was reallocated, which clears its contents */
    bool EnsureHostBufferCapacity(StringID name, VkDeviceSize requiredSize);

    HostBuffer& GetHostBuffer(StringID name);

    HostBufferSlots& GetHostBufferSlots(StringID name);

    /** Queues the pass that carries a non-REBAR slot's mirror into its device buffer. Returns the staging offset the caller's regions are relative to. */
    VkDeviceSize QueueHostBufferStagingCopy(StringID name, VkDeviceSize size, bool bFullCopy);

    /** Imports the current frame's slot into the pass system so passes can declare reads against it. */
    void ImportHostBuffer(StringID name);
    const RenderPass* currentRecordingPass{};
    bool bFrameCorrupted{};

    VulkanContext* context;
    ResourceManager* resourceManager;
    Core::TlsfAllocator* alloc;
    Core::Arena* arena;
    RenderGraphAllocFns allocFns;

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
    Core::HandleAllocator<BufferResource, RDG_MAX_TLAS> transientASHandleAllocator;

    Core::ArenaFixedVector<BufferResource> buffers;
    Core::ArenaFixedMap<StringID, uint32_t> bufferNameToIndex;

    // Physical resources
    Core::Vector<PhysicalResource> physicalResources;

    // Render passes
    Core::ArenaFixedVector<RenderPass*> passes;
    // Generated at compile time
    Core::ArenaFixedVector<RenderPass*> sortedPasses;
    Core::Vector<uint32_t> waveOffsets;

    uint32_t asyncPassCount{0};
    uint32_t asyncWaveCount{0};
    VkPipelineStageFlags2 crossCutWaitStageMask{0};

    struct WaveBarrierRange
    {
        uint32_t preClearImageStart;
        uint32_t preClearImageCount;
        uint32_t imageStart;
        uint32_t imageCount;
        uint32_t bufferStart;
        uint32_t bufferCount;
    };
    Core::Vector<VkImageMemoryBarrier2> compiledImageBarriers;
    Core::Vector<VkBufferMemoryBarrier2> compiledBufferBarriers;
    Core::Vector<WaveBarrierRange> compiledWaveRanges;

    Core::Vector<ResourceRing> rings;

    uint32_t currentFrameIndex{0};
    Core::Array<TransientUploadArena, Core::FRAME_BUFFER_COUNT> uploadArenas{};
    Core::Array<TransientReadback, Core::FRAME_BUFFER_COUNT> meshletCountReadbacks{};
    Core::InlineVector<HostBufferSlots, 32> hostBuffers{};
    Core::InlineVector<RetiredBuffer, 32> retiredBuffers{};
    GPUTimestampQueryPool gpuTimestampQuery{};
    GPUProfileSnapshot lastGpuProfile{};

    bool bRemoveSwapchainPhysicals{false};
    bool bDestroyViewportAssociated{false};
    bool bDropAllRings{false};

    bool bDebugLogging = false;
    uint32_t debugCaptureFramesLeft{0};
    uint32_t debugNameCounter{0};

private:
    TextureResource* GetTexture(StringID imageId);

    TextureResource* GetOrCreateTexture(StringID textureId);

    BufferResource* GetBuffer(StringID bufferId);

    BufferResource* GetOrCreateBuffer(StringID textureId);

    /**
     * Releases a logical from the physical it was bound to so the compile pass allocates it fresh; the previous contents are lost.
     * Only the logical side is touched here; ReconcileDetachedPhysicals does the physical bookkeeping at compile time.
     * @param tex
     */
    void DetachTexture(TextureResource& tex) const;

    void DetachBuffer(BufferResource& buf) const;

    /**
     * Clears physical to logical back-references left dangling by a detach, re-opening any physical that is now unreferenced for aliasing this frame.
     */
    void ReconcileDetachedPhysicals();

private: // Rings
    ResourceRing* FindRing(StringID name);

    void ResetRing(ResourceRing& ring);

    /** Shifts the ages for Fresh/Emplaced, then creates the logicals for every existing version and binds them to their physicals; the bare name is fresh, absent, or slot 0 by source. */
    void BindRingLogicals(ResourceRing& ring);

    /** Frame end: drops undeclared rings, refreshes image layouts, and captures the physical of a pending slot 0. */
    void CaptureRingVersions();

    void OnPhysicalRemoved(uint32_t physicalIndex);

private: // Physicals

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
