//
// Render graph test suite. Drafted by Claude.
//
// Philosophy: these tests drive the *whole* compile pipeline (Reset -> register -> AddPass -> Compile).
//  It asserts the deterministic CPU products: the scheduled DAG, wave layering, accumulated usage, lifetimes, physical-resource aliasing, and (the real payload) the flat barrier arrays produced by PrecomputeBarriers.
//  A single Execute test confirms passes fire and descriptor indices resolve inside the executeFunc lambda.
//
// Everything runs CPU-only: the RenderGraphAllocFns seam stubs all GPU allocation and the command-buffer recording calls, so Execute() replays its precomputed barriers without dispatching into a real driver.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <unordered_map>

#include "core/hash/xxh3.h"
#include "core/memory/arena.h"
#include "core/memory/handle.h"
#include "core/memory/tlsf_allocator.h"
#include "render/render-graph/render_graph.h"
#include "render/render-graph/render_graph_resources.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_context.h"

namespace Render
{
/**
 * White-box, friends with both render graph and render pass
 */
class RenderGraphInspector
{
public:
    explicit RenderGraphInspector(RenderGraph& rdg) : rdg(rdg) {}

    // ---- DAG / scheduling -----------------------------------------------------------------
    [[nodiscard]] uint32_t SortedPassCount() const { return static_cast<uint32_t>(rdg.sortedPasses.Size()); }

    [[nodiscard]] uint32_t WaveCount() const { return rdg.waveOffsets.Size() > 0 ? static_cast<uint32_t>(rdg.waveOffsets.Size() - 1) : 0; }

    [[nodiscard]] uint32_t PassesInWave(uint32_t wave) const { return rdg.waveOffsets[wave + 1] - rdg.waveOffsets[wave]; }

    [[nodiscard]] uint32_t PassWaveIndex(StringID passId) const
    {
        for (auto& pass : rdg.sortedPasses) {
            if (pass->renderPassId == passId) { return pass->waveIndex; }
        }
        return UINT32_MAX;
    }

    [[nodiscard]] uint32_t PassSortedIndex(StringID passId) const
    {
        for (uint32_t i = 0; i < rdg.sortedPasses.Size(); ++i) {
            if (rdg.sortedPasses[i]->renderPassId == passId) { return i; }
        }
        return UINT32_MAX;
    }

    [[nodiscard]] bool PassComesBeforeInSort(StringID earlier, StringID later) const { return PassSortedIndex(earlier) < PassSortedIndex(later); }

    // Creation-order index in passes[] (the index space used by inEdges/outEdges). Stable across
    // the sort, which only mutates passIndex/inDegree/waveIndex, never the passes[] vector.
    [[nodiscard]] uint32_t CreationIndex(StringID passId) const
    {
        for (uint32_t i = 0; i < rdg.passes.Size(); ++i) {
            if (rdg.passes[i]->renderPassId == passId) { return i; }
        }
        return UINT32_MAX;
    }

    // Exact adjacency check via outEdges. Valid after a full Compile().
    [[nodiscard]] bool HasEdge(StringID from, StringID to) const
    {
        const uint32_t fi = CreationIndex(from);
        const uint32_t ti = CreationIndex(to);
        if (fi == UINT32_MAX || ti == UINT32_MAX) { return false; }
        for (const uint32_t e : rdg.passes[fi]->outEdges) {
            if (e == ti) { return true; }
        }
        return false;
    }

    // ---- Usage ----------------------------------------------------------------------------
    [[nodiscard]] VkImageUsageFlags TextureAccumulatedUsage(RDGTexture texture) const
    {
        const TextureResource* t = Tex(texture);
        return t ? t->accumulatedUsage : 0;
    }

    [[nodiscard]] VkBufferUsageFlags BufferAccumulatedUsage(RDGBuffer buffer) const
    {
        const BufferResource* b = Buf(buffer);
        return b ? b->accumulatedUsage : 0;
    }

    // ---- Lifetimes ------------------------------------------------------------------------
    [[nodiscard]] uint32_t TextureFirstPass(RDGTexture texture) const
    {
        const TextureResource* t = Tex(texture);
        return t ? t->firstPass : UINT32_MAX;
    }

    [[nodiscard]] uint32_t TextureLastPass(RDGTexture texture) const
    {
        const TextureResource* t = Tex(texture);
        return t ? t->lastPass : UINT32_MAX;
    }

    [[nodiscard]] uint32_t BufferFirstPass(RDGBuffer buffer) const
    {
        const BufferResource* b = Buf(buffer);
        return b ? b->firstPass : UINT32_MAX;
    }

    [[nodiscard]] uint32_t BufferLastPass(RDGBuffer buffer) const
    {
        const BufferResource* b = Buf(buffer);
        return b ? b->lastPass : UINT32_MAX;
    }

    // ---- Physical resources ---------------------------------------------------------------
    [[nodiscard]] size_t PhysicalCount() const { return rdg.physicalResources.Size(); }

    [[nodiscard]] uint32_t TexturePhysicalIndex(RDGTexture texture) const
    {
        const TextureResource* t = Tex(texture);
        return t ? t->physicalIndex : UINT32_MAX;
    }

    [[nodiscard]] uint32_t BufferPhysicalIndex(RDGBuffer buffer) const
    {
        const BufferResource* b = Buf(buffer);
        return b ? b->physicalIndex : UINT32_MAX;
    }

    [[nodiscard]] bool TexturesSharePhysical(RDGTexture a, RDGTexture b) const
    {
        const uint32_t ia = TexturePhysicalIndex(a);
        const uint32_t ib = TexturePhysicalIndex(b);
        return ia != UINT32_MAX && ia == ib;
    }

    [[nodiscard]] bool BuffersSharePhysical(RDGBuffer a, RDGBuffer b) const
    {
        const uint32_t ia = BufferPhysicalIndex(a);
        const uint32_t ib = BufferPhysicalIndex(b);
        return ia != UINT32_MAX && ia == ib;
    }

    [[nodiscard]] bool PhysicalIsImported(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].bIsImported; }
    [[nodiscard]] bool PhysicalCanAlias(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].bCanAlias; }
    [[nodiscard]] bool PhysicalIsViewportScaled(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].bIsViewportScaled; }
    [[nodiscard]] bool PhysicalIsSwapchain(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].bIsSwapchain; }
    [[nodiscard]] bool PhysicalDisableBarriers(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].bDisableBarriers; }
    [[nodiscard]] bool PhysicalIsAllocated(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].IsAllocated(); }
    [[nodiscard]] bool PhysicalAddressRetrieved(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].addressRetrieved; }

    [[nodiscard]] VkImageUsageFlags PhysicalImageUsage(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].dimensions.imageUsage : 0; }
    [[nodiscard]] VkBufferUsageFlags PhysicalBufferUsage(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].dimensions.bufferUsage : 0; }
    [[nodiscard]] VkImageAspectFlags PhysicalAspect(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].aspect : 0; }
    [[nodiscard]] VkImage PhysicalImage(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].image : VK_NULL_HANDLE; }
    [[nodiscard]] VkBuffer PhysicalBuffer(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].buffer : VK_NULL_HANDLE; }
    [[nodiscard]] VkDeviceAddress PhysicalBufferAddress(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].bufferAddress : 0; }
    [[nodiscard]] uint64_t PhysicalLastUsedFrame(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].lastUsedFrame : 0; }
    [[nodiscard]] size_t PhysicalLogicalCount(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].logicalResourceIndices.Size() : 0; }
    [[nodiscard]] VkPipelineStageFlags2 PhysicalEventStages(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].event.stages : 0; }
    [[nodiscard]] VkAccessFlags2 PhysicalEventAccess(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].event.access : 0; }

    [[nodiscard]] bool PhysicalSampledDescriptorValid(uint32_t i) const { return Valid(i) && rdg.physicalResources[i].sampledDescriptorHandle.IsValid(); }
    [[nodiscard]] uint32_t PhysicalSampledDescriptorIndex(uint32_t i) const { return Valid(i) ? rdg.physicalResources[i].sampledDescriptorHandle.index : Core::INVALID_HANDLE_INDEX; }

    [[nodiscard]] VkImage TextureImage(RDGTexture texture) const { return PhysicalImage(TexturePhysicalIndex(texture)); }
    [[nodiscard]] VkBuffer BufferHandle(RDGBuffer buffer) const { return PhysicalBuffer(BufferPhysicalIndex(buffer)); }

    // ---- State ----------------------------------------------------------------------------
    [[nodiscard]] VkImageLayout TextureLayout(RDGTexture texture) const
    {
        const TextureResource* t = Tex(texture);
        return t ? t->layout : VK_IMAGE_LAYOUT_UNDEFINED;
    }

    [[nodiscard]] VkImageLayout TextureFinalLayout(RDGTexture texture) const
    {
        const TextureResource* t = Tex(texture);
        return t ? t->finalLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    }

    // ---- Auto-clear -----------------------------------------------------------------------
    [[nodiscard]] bool TextureHasClearInPass(RDGTexture texture, StringID passId) const
    {
        const TextureResource* t = Tex(texture);
        if (!t) { return false; }
        for (auto& pass : rdg.sortedPasses) {
            if (pass->renderPassId == passId) {
                for (uint32_t idx : pass->autoClearTextures) {
                    if (idx == t->index) { return true; }
                }
            }
        }
        return false;
    }

    // ---- Versioned resources --------------------------------------------------------------
    [[nodiscard]] size_t RingCount() const { return rdg.rings.Size(); }

    // ---- Barriers (the real product of PrecomputeBarriers) --------------------------------
    [[nodiscard]] size_t TotalImageBarriers() const { return rdg.compiledImageBarriers.Size(); }
    [[nodiscard]] size_t TotalBufferBarriers() const { return rdg.compiledBufferBarriers.Size(); }
    [[nodiscard]] uint32_t BarrierWaveCount() const { return static_cast<uint32_t>(rdg.compiledWaveRanges.Size()); }

    [[nodiscard]] uint32_t WavePreClearCount(uint32_t wave) const { return rdg.compiledWaveRanges[wave].preClearImageCount; }
    [[nodiscard]] uint32_t WaveImageBarrierCount(uint32_t wave) const { return rdg.compiledWaveRanges[wave].imageCount; }
    [[nodiscard]] uint32_t WaveBufferBarrierCount(uint32_t wave) const { return rdg.compiledWaveRanges[wave].bufferCount; }

    // Confirms the flat-array offset arithmetic is contiguous: pre-clear then main image range.
    [[nodiscard]] bool WaveRangesContiguous(uint32_t wave) const
    {
        const auto& r = rdg.compiledWaveRanges[wave];
        return r.imageStart == r.preClearImageStart + r.preClearImageCount;
    }

    [[nodiscard]] const VkImageMemoryBarrier2* FindImageBarrier(uint32_t wave, RDGTexture texture) const
    {
        VkImage img = TextureImage(texture);
        const auto& r = rdg.compiledWaveRanges[wave];
        for (uint32_t i = 0; i < r.imageCount; ++i) {
            const VkImageMemoryBarrier2& b = rdg.compiledImageBarriers[r.imageStart + i];
            if (b.image == img) { return &b; }
        }
        return nullptr;
    }

    [[nodiscard]] const VkImageMemoryBarrier2* FindPreClearBarrier(uint32_t wave, RDGTexture texture) const
    {
        VkImage img = TextureImage(texture);
        const auto& r = rdg.compiledWaveRanges[wave];
        for (uint32_t i = 0; i < r.preClearImageCount; ++i) {
            const VkImageMemoryBarrier2& b = rdg.compiledImageBarriers[r.preClearImageStart + i];
            if (b.image == img) { return &b; }
        }
        return nullptr;
    }

    [[nodiscard]] const VkBufferMemoryBarrier2* FindBufferBarrier(uint32_t wave, RDGBuffer buffer) const
    {
        VkBuffer buf = BufferHandle(buffer);
        const auto& r = rdg.compiledWaveRanges[wave];
        for (uint32_t i = 0; i < r.bufferCount; ++i) {
            const VkBufferMemoryBarrier2& b = rdg.compiledBufferBarriers[r.bufferStart + i];
            if (b.buffer == buf) { return &b; }
        }
        return nullptr;
    }

private:
    [[nodiscard]] bool Valid(uint32_t i) const { return i < rdg.physicalResources.Size(); }

    [[nodiscard]] const TextureResource* Tex(RDGTexture texture) const { return texture.IsValid() && texture.index < rdg.textures.Size() ? &rdg.textures[texture.index] : nullptr; }

    [[nodiscard]] const BufferResource* Buf(RDGBuffer buffer) const { return buffer.IsValid() && buffer.index < rdg.buffers.Size() ? &rdg.buffers[buffer.index] : nullptr; }

    RenderGraph& rdg;
};
} // Render

// ---- Stubs ----------------------------------------------------------------------------------

// Real host backing for stubbed buffers so the transient uploader's pointer writes and
// grow-time memcpy operate on valid memory. Keyed by the fake VkBuffer handle; freed on destroy.
namespace
{
std::unordered_map<VkBuffer, void*>& BufferBackings()
{
    static std::unordered_map<VkBuffer, void*> m;
    return m;
}
} // namespace

// Allocation seam: hands out unique non-null fake handles and real host memory for buffers.
// createImageView returns non-null so the descriptor-write block in AssignPhysicalResources runs;
// writeDescriptors then fabricates valid descriptor handles (the real branch would deref a null
// ResourceManager). Buffer device addresses are a deterministic hash of the handle.
static Render::RenderGraphAllocFns MakeStubAllocFns()
{
    Render::RenderGraphAllocFns fns;
    fns.createImage = [](const Render::VulkanContext*, const VkImageCreateInfo&) -> Render::RenderGraphAllocFns::ImageAlloc {
        static uint64_t counter = 1;
        return {reinterpret_cast<VkImage>(counter++), VK_NULL_HANDLE};
    };
    fns.createImageView = [](const Render::VulkanContext*, const VkImageViewCreateInfo&) -> VkImageView {
        static uint64_t counter = 1;
        return reinterpret_cast<VkImageView>(counter++);
    };
    fns.destroyImage = [](const Render::VulkanContext*, VkImage, VmaAllocation) {};
    fns.destroyImageView = [](const Render::VulkanContext*, VkImageView) {};
    fns.createBuffer = [](const Render::VulkanContext*, const VkBufferCreateInfo& ci, const VmaAllocationCreateInfo&) -> Render::RenderGraphAllocFns::BufferAlloc {
        static uint64_t counter = 1;
        auto buffer = reinterpret_cast<VkBuffer>(counter++);
        void* mapped = ci.size > 0 ? std::malloc(static_cast<size_t>(ci.size)) : nullptr;
        if (mapped) { BufferBackings()[buffer] = mapped; }
        return {buffer, VK_NULL_HANDLE, mapped};
    };
    fns.destroyBuffer = [](const Render::VulkanContext*, VkBuffer buffer, VmaAllocation) {
        auto& m = BufferBackings();
        const auto it = m.find(buffer);
        if (it != m.end()) {
            std::free(it->second);
            m.erase(it);
        }
    };
    fns.getBufferDeviceAddress = [](const Render::VulkanContext*, VkBuffer buffer) -> VkDeviceAddress {
        auto val = reinterpret_cast<uint64_t>(buffer);
        return Hash(&val, sizeof(val));
    };
    fns.writeDescriptors = [](Render::PhysicalResource& phys) {
        static uint32_t descCounter = 1;
        phys.sampledDescriptorHandle = {descCounter++, 1};
        for (uint32_t mip = 0; mip < phys.dimensions.levels; ++mip) {
            phys.storageMipDescriptorHandles[mip] = {descCounter++, 1};
        }
        phys.depthOnlyDescriptorHandle = {descCounter++, 1};
        phys.stencilOnlyDescriptorHandle = {descCounter++, 1};
    };
    fns.setDebugName = [](const Render::VulkanContext*, VkObjectType, uint64_t, const char*) {};
    // Command-buffer seam: no-op so Execute() can replay the precomputed barriers/clears without a real device.
    fns.cmdPipelineBarrier2 = [](VkCommandBuffer, const VkDependencyInfo*) {};
    fns.cmdClearColorImage = [](VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue*, uint32_t, const VkImageSubresourceRange*) {};
    fns.cmdClearDepthStencilImage = [](VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue*, uint32_t, const VkImageSubresourceRange*) {};
    fns.cmdBeginDebugUtilsLabel = [](VkCommandBuffer, const VkDebugUtilsLabelEXT*) {};
    fns.cmdEndDebugUtilsLabel = [](VkCommandBuffer) {};
    return fns;
}

// ---- Fixture ------------------------------------------------------------------------------

struct AllocBase
{
    static constexpr size_t TLSF_SIZE = 4 * 1024 * 1024;
    static constexpr size_t ARENA_SIZE = 16 * 1024 * 1024;

    std::unique_ptr<char[]> tlsfPool{new char[TLSF_SIZE]};
    std::unique_ptr<char[]> arenaPool{new char[ARENA_SIZE]};

    Core::TlsfAllocator alloc;
    Core::Arena arena;

    AllocBase() : arena(arenaPool.get(), ARENA_SIZE, "rdg-test")
    {
        alloc.Init(tlsfPool.get(), TLSF_SIZE, false, "rdg-test");
    }
};

struct RdgFixture : AllocBase
{
    struct FakeContextHolder
    {
        // VulkanContext's destructor tears down real Vulkan/VMA objects (vmaDestroyAllocator,
        // vkDestroyDevice, ...). Our fake context exists only to satisfy RenderGraph's
        // "allocator != nullptr" construction assert; its handles are fake (allocator == 1,
        // everything else null) and volk is never loaded in the test binary, so letting that
        // destructor run crashes at exit. We placement-new the context into static storage and
        // intentionally never destroy it. The single leak is reclaimed by the OS at process exit.
        alignas(Render::VulkanContext) unsigned char storage[sizeof(Render::VulkanContext)]{};
        Render::VulkanContext& ctx;

        FakeContextHolder()
            : ctx(*new(&storage) Render::VulkanContext())
        {
            ctx.allocator = reinterpret_cast<VmaAllocator>(uintptr_t{1});
        }
    };

    inline static FakeContextHolder fakeContextHolder{};

    Render::RenderGraph rdg;
    Render::RenderGraphInspector inspector;

    RdgFixture()
        : rdg(&fakeContextHolder.ctx, nullptr, alloc, arena, MakeStubAllocFns()),
          inspector(rdg)
    {
        rdg.FrameStartReset(0, 0, 100);
    }

    /**
     * A valid "any" color texture: a real format so it satisfies CreateTexture's format contract.
     */
    static Render::TextureInfo TexInfo(uint32_t w = 1920, uint32_t h = 1080, uint32_t mips = 1)
    {
        return {VK_FORMAT_R16G16B16A16_SFLOAT, w, h, mips};
    }

    /**
     *  Real depth format: aspect resolves to DEPTH for depth-attachment barrier tests.
     */
    static Render::TextureInfo DepthTexInfo(uint32_t w = 1920, uint32_t h = 1080)
    {
        return {VK_FORMAT_D32_SFLOAT, w, h, 1};
    }

    void Compile(int64_t frame = 0) { rdg.Compile(frame); }
    void NextFrame(uint32_t frameIdx = 0, uint64_t frame = 1, uint64_t maxUnused = 100) { rdg.FrameStartReset(frameIdx, frame, maxUnused); }
};

using namespace Render;

// ================================================================================
// Section 1: End-to-end mini-frame
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: mini-frame schedule, usage, aliasing and barriers", "[rdg][e2e]")
{
    VkClearValue depthClear{};
    depthClear.depthStencil = {1.0f, 0};

    auto swapImg = reinterpret_cast<VkImage>(0x5A11C0DEull);
    auto swapView = reinterpret_cast<VkImageView>(0x5A11C0DFull);

    const RDGTexture swap = rdg.ImportTexture("swap"_sid, swapImg, swapView, TexInfo(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                              VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true);
    const RDGTexture depth = rdg.CreateTexture("depth"_sid, DepthTexInfo(), depthClear);
    const RDGTexture gbuffer = rdg.CreateTexture("gbuffer"_sid, TexInfo());
    const RDGTexture hdr = rdg.CreateTexture("hdr"_sid, TexInfo());
    const RDGBuffer drawBuf = rdg.CreateBuffer("drawBuf"_sid, 4096);
    const RDGBuffer indirectBuf = rdg.CreateBuffer("indirectBuf"_sid, 4096);

    rdg.AddPass("cull"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged)
            .WriteBuffer(drawBuf)
            .WriteBuffer(indirectBuf);
    rdg.AddPass("depthPrepass"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged)
            .WriteDepthAttachment(depth);
    rdg.AddPass("gbuffer"_sid, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, Render::RenderCategory::Untagged)
            .WriteColorAttachment(gbuffer)
            .ReadDepthAttachment(depth)
            .ReadIndirectBuffer(indirectBuf)
            .ReadBuffer(drawBuf);
    rdg.AddPass("lighting"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged)
            .ReadSampledImage(gbuffer)
            .WriteStorageImage(hdr);
    rdg.AddPass("post"_sid, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, Render::RenderCategory::Untagged)
            .ReadSampledImage(hdr)
            .WriteColorAttachment(swap);

    Compile();

    // Schedule
    CHECK(inspector.SortedPassCount() == 5);
    CHECK(inspector.WaveCount() == 4);
    CHECK(inspector.PassWaveIndex("cull"_sid) == 0);
    CHECK(inspector.PassWaveIndex("depthPrepass"_sid) == 0);
    CHECK(inspector.PassWaveIndex("gbuffer"_sid) == 1);
    CHECK(inspector.PassWaveIndex("lighting"_sid) == 2);
    CHECK(inspector.PassWaveIndex("post"_sid) == 3);

    // Dependency edges
    CHECK(inspector.HasEdge("cull"_sid, "gbuffer"_sid));
    CHECK(inspector.HasEdge("depthPrepass"_sid, "gbuffer"_sid));
    CHECK(inspector.HasEdge("gbuffer"_sid, "lighting"_sid));
    CHECK(inspector.HasEdge("lighting"_sid, "post"_sid));
    CHECK_FALSE(inspector.HasEdge("cull"_sid, "depthPrepass"_sid));

    // Usage
    CHECK((inspector.BufferAccumulatedUsage(drawBuf) & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(indirectBuf) & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(depth) & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(depth) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0); // clear value
    CHECK((inspector.TextureAccumulatedUsage(gbuffer) & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(gbuffer) & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(hdr) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);

    // Imported swapchain: separate physical, present final layout stored.
    CHECK(inspector.PhysicalIsImported(inspector.TexturePhysicalIndex(swap)));
    CHECK(inspector.PhysicalIsSwapchain(inspector.TexturePhysicalIndex(swap)));
    CHECK(inspector.TextureFinalLayout(swap) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // gbuffer (waves 1-2) and hdr (waves 2-3) overlap at wave 2 -> cannot alias.
    CHECK_FALSE(inspector.TexturesSharePhysical(gbuffer, hdr));
    CHECK(inspector.PhysicalCount() >= 6);

    // Auto-clear: depth is cleared in its first pass (depthPrepass, wave 0) and gets a pre-clear barrier.
    CHECK(inspector.TextureHasClearInPass(depth, "depthPrepass"_sid));
    const VkImageMemoryBarrier2* depthPreClear = inspector.FindPreClearBarrier(0, depth);
    REQUIRE(depthPreClear != nullptr);
    CHECK(depthPreClear->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // gbuffer color-attachment transition in its producing wave.
    const VkImageMemoryBarrier2* gbufBarrier = inspector.FindImageBarrier(1, gbuffer);
    REQUIRE(gbufBarrier != nullptr);
    CHECK(gbufBarrier->newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // drawBuf is read in wave 1 after being written in wave 0: RAW buffer barrier present.
    const VkBufferMemoryBarrier2* drawBarrier = inspector.FindBufferBarrier(1, drawBuf);
    REQUIRE(drawBarrier != nullptr);
    CHECK((drawBarrier->dstAccessMask & VK_ACCESS_2_SHADER_READ_BIT) != 0);
}

// ================================================================================
// Section 2: Scheduling & dependency edges
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: single pass is wave 0 with no edges", "[rdg][schedule]")
{
    rdg.AddPass("only"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged);
    Compile();
    CHECK(inspector.SortedPassCount() == 1);
    CHECK(inspector.WaveCount() == 1);
    CHECK(inspector.PassWaveIndex("only"_sid) == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: independent passes share wave 0 and have no edges", "[rdg][schedule]")
{
    const RDGTexture a = rdg.CreateTexture("a"_sid, TexInfo());
    const RDGTexture b = rdg.CreateTexture("b"_sid, TexInfo(640, 480));
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(a);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(b);
    Compile();
    CHECK(inspector.WaveCount() == 1);
    CHECK_FALSE(inspector.HasEdge("pA"_sid, "pB"_sid));
    CHECK_FALSE(inspector.HasEdge("pB"_sid, "pA"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: linear chain produces sequential waves and edges", "[rdg][schedule]")
{
    const RDGTexture t0 = rdg.CreateTexture("t0"_sid, TexInfo());
    const RDGTexture t1 = rdg.CreateTexture("t1"_sid, TexInfo(640, 480));
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(t0);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(t0).WriteStorageImage(t1);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(t1);
    Compile();
    CHECK(inspector.WaveCount() == 3);
    CHECK(inspector.PassWaveIndex("pA"_sid) == 0);
    CHECK(inspector.PassWaveIndex("pB"_sid) == 1);
    CHECK(inspector.PassWaveIndex("pC"_sid) == 2);
    CHECK(inspector.HasEdge("pA"_sid, "pB"_sid));
    CHECK(inspector.HasEdge("pB"_sid, "pC"_sid));
    CHECK_FALSE(inspector.HasEdge("pA"_sid, "pC"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: diamond has exactly the four expected edges, no cross edge", "[rdg][schedule]")
{
    const RDGTexture in = rdg.CreateTexture("in"_sid, TexInfo());
    const RDGTexture b = rdg.CreateTexture("b"_sid, TexInfo(640, 480));
    const RDGTexture c = rdg.CreateTexture("c"_sid, TexInfo(320, 240));
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(in);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(in).WriteStorageImage(b);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(in).WriteStorageImage(c);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(b).ReadStorageImage(c);
    Compile();
    CHECK(inspector.HasEdge("pA"_sid, "pB"_sid));
    CHECK(inspector.HasEdge("pA"_sid, "pC"_sid));
    CHECK(inspector.HasEdge("pB"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pC"_sid, "pD"_sid));
    CHECK_FALSE(inspector.HasEdge("pB"_sid, "pC"_sid));
    CHECK_FALSE(inspector.HasEdge("pC"_sid, "pB"_sid));
    CHECK(inspector.PassWaveIndex("pB"_sid) == inspector.PassWaveIndex("pC"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: write-read-write produces RAW, WAR and WAW edges", "[rdg][schedule]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("p0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("p1"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    rdg.AddPass("p2"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    Compile();
    CHECK(inspector.HasEdge("p0"_sid, "p1"_sid)); // RAW
    CHECK(inspector.HasEdge("p1"_sid, "p2"_sid)); // WAR
    CHECK(inspector.HasEdge("p0"_sid, "p2"_sid)); // WAW
}

TEST_CASE_METHOD(RdgFixture, "RDG: same-layout readers depend on the transition cause, not each other", "[rdg][schedule]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    Compile();
    CHECK(inspector.HasEdge("pA"_sid, "pB"_sid));
    CHECK(inspector.HasEdge("pA"_sid, "pC"_sid));
    CHECK_FALSE(inspector.HasEdge("pB"_sid, "pC"_sid));
    CHECK(inspector.PassWaveIndex("pB"_sid) == inspector.PassWaveIndex("pC"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: readers in different layouts serialize on layout change", "[rdg][schedule]")
{
    // Storage-read (GENERAL) then sampled-read (SHADER_READ_ONLY) of the same texture: the second
    // reader must wait on the first because the layout epoch changes.
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("p0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("pStorageRead"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    rdg.AddPass("pSampledRead"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    Compile();
    CHECK(inspector.HasEdge("pStorageRead"_sid, "pSampledRead"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer RAW, WAW and WAR edges", "[rdg][schedule]")
{
    const RDGBuffer buf = rdg.CreateBuffer("buf"_sid, 1024);
    rdg.AddPass("p0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);
    rdg.AddPass("p1"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(buf);
    rdg.AddPass("p2"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);
    Compile();
    CHECK(inspector.HasEdge("p0"_sid, "p1"_sid));
    CHECK(inspector.HasEdge("p1"_sid, "p2"_sid));
    CHECK(inspector.HasEdge("p0"_sid, "p2"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: cross-epoch RAWs on same texture both produce correct edges", "[rdg][schedule]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    const RDGTexture other = rdg.CreateTexture("other"_sid, TexInfo(640, 480));
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(other);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex).ReadStorageImage(other);
    rdg.AddPass("pE"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    Compile();
    CHECK(inspector.HasEdge("pA"_sid, "pB"_sid));
    CHECK(inspector.HasEdge("pB"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pC"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pD"_sid, "pE"_sid));
    CHECK(inspector.HasEdge("pA"_sid, "pE"_sid));
    CHECK(inspector.PassWaveIndex("pD"_sid) > inspector.PassWaveIndex("pB"_sid));
    CHECK(inspector.PassWaveIndex("pD"_sid) > inspector.PassWaveIndex("pC"_sid));
    CHECK(inspector.PassWaveIndex("pE"_sid) > inspector.PassWaveIndex("pD"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: parallel texture readers both produce WAR edges to next write", "[rdg][schedule]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    Compile();
    CHECK(inspector.HasEdge("pA"_sid, "pB"_sid));
    CHECK(inspector.HasEdge("pA"_sid, "pC"_sid));
    CHECK(inspector.HasEdge("pB"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pC"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pA"_sid, "pD"_sid));
    CHECK_FALSE(inspector.HasEdge("pB"_sid, "pC"_sid));
    CHECK(inspector.PassWaveIndex("pB"_sid) == inspector.PassWaveIndex("pC"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: parallel readers both edge to reader in new layout", "[rdg][schedule]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    Compile();
    CHECK(inspector.HasEdge("pB"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pC"_sid, "pD"_sid));
    CHECK_FALSE(inspector.HasEdge("pB"_sid, "pC"_sid));
    CHECK(inspector.PassWaveIndex("pB"_sid) == inspector.PassWaveIndex("pC"_sid));
    CHECK(inspector.PassWaveIndex("pD"_sid) > inspector.PassWaveIndex("pB"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: parallel buffer readers both produce WAR edges to next write", "[rdg][schedule]")
{
    const RDGBuffer buf = rdg.CreateBuffer("buf"_sid, 1024);
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(buf);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(buf);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);
    Compile();
    CHECK(inspector.HasEdge("pA"_sid, "pB"_sid));
    CHECK(inspector.HasEdge("pA"_sid, "pC"_sid));
    CHECK(inspector.HasEdge("pB"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pC"_sid, "pD"_sid));
    CHECK(inspector.HasEdge("pA"_sid, "pD"_sid));
    CHECK_FALSE(inspector.HasEdge("pB"_sid, "pC"_sid));
    CHECK(inspector.PassWaveIndex("pB"_sid) == inspector.PassWaveIndex("pC"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: two producers feeding a merger put the merger one wave later", "[rdg][schedule]")
{
    const RDGTexture tA = rdg.CreateTexture("tA"_sid, TexInfo());
    const RDGTexture tB = rdg.CreateTexture("tB"_sid, TexInfo(640, 480));
    rdg.AddPass("prodA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tA);
    rdg.AddPass("prodB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tB);
    rdg.AddPass("merge"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tA).ReadStorageImage(tB);
    Compile();
    CHECK(inspector.WaveCount() == 2);
    CHECK(inspector.PassWaveIndex("prodA"_sid) == 0);
    CHECK(inspector.PassWaveIndex("prodB"_sid) == 0);
    CHECK(inspector.PassWaveIndex("merge"_sid) == 1);
}

// ================================================================================
// Section 3: Usage accumulation (exact masks)
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: storage write/read sets STORAGE_BIT", "[rdg][usage]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("r"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(tex) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: sampled read sets SAMPLED_BIT", "[rdg][usage]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(tex) & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: color attachment sets COLOR_ATTACHMENT_BIT", "[rdg][usage]")
{
    const RDGTexture color = rdg.CreateTexture("color"_sid, TexInfo());
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, Render::RenderCategory::Untagged).WriteColorAttachment(color);
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(color) & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: depth attachment sets DEPTH_STENCIL and SAMPLED", "[rdg][usage]")
{
    const RDGTexture depth = rdg.CreateTexture("depth"_sid, TexInfo());
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).WriteDepthAttachment(depth);
    rdg.AccumulateUsage();
    const VkImageUsageFlags usage = inspector.TextureAccumulatedUsage(depth);
    CHECK((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
    CHECK((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: blit and copy read/write set TRANSFER_SRC/DST", "[rdg][usage]")
{
    const RDGTexture bsrc = rdg.CreateTexture("bsrc"_sid, TexInfo());
    const RDGTexture bdst = rdg.CreateTexture("bdst"_sid, TexInfo(640, 480));
    const RDGTexture csrc = rdg.CreateTexture("csrc"_sid, TexInfo(320, 240));
    const RDGTexture cdst = rdg.CreateTexture("cdst"_sid, TexInfo(160, 120));
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged)
            .ReadBlitImage(bsrc).WriteBlitImage(bdst).ReadCopyImage(csrc).WriteCopyImage(cdst);
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(bsrc) & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(bdst) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(csrc) & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(cdst) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: clear-value texture gets TRANSFER_DST automatically", "[rdg][usage]")
{
    VkClearValue cv{};
    cv.color = {{0.f, 0.f, 0.f, 1.f}};
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo(), cv);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(tex) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer write sets STORAGE and DEVICE_ADDRESS", "[rdg][usage]")
{
    const RDGBuffer buf = rdg.CreateBuffer("buf"_sid, 1024);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);
    rdg.AccumulateUsage();
    const VkBufferUsageFlags usage = inspector.BufferAccumulatedUsage(buf);
    CHECK((usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0);
    CHECK((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: index, indirect and indirect-count buffer usages", "[rdg][usage]")
{
    const RDGBuffer idx = rdg.CreateBuffer("idx"_sid, 1024);
    const RDGBuffer indirect = rdg.CreateBuffer("indirect"_sid, 1024);
    const RDGBuffer count = rdg.CreateBuffer("count"_sid, 1024);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, Render::RenderCategory::Untagged)
            .ReadIndexBuffer(idx).ReadIndirectBuffer(indirect).ReadIndirectCountBuffer(count);
    rdg.AccumulateUsage();
    CHECK((inspector.BufferAccumulatedUsage(idx) & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(indirect) & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(count) & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: transfer buffer read/write set TRANSFER_SRC/DST", "[rdg][usage]")
{
    const RDGBuffer src = rdg.CreateBuffer("src"_sid, 1024);
    const RDGBuffer dst = rdg.CreateBuffer("dst"_sid, 1024);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged).ReadTransferBuffer(src).WriteTransferBuffer(dst);
    rdg.AccumulateUsage();
    CHECK((inspector.BufferAccumulatedUsage(src) & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(dst) & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: multiple access types union into accumulated usage", "[rdg][usage]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("r"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    rdg.AccumulateUsage();
    const VkImageUsageFlags usage = inspector.TextureAccumulatedUsage(tex);
    CHECK((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
    CHECK((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

// ================================================================================
// Section 4: Lifetimes
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: lifetime spans first and last sorted pass that use a texture", "[rdg][lifetime]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(tex);
    Compile();
    CHECK(inspector.TextureFirstPass(tex) == 0);
    CHECK(inspector.TextureLastPass(tex) == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: middle-only resource has a tight lifetime", "[rdg][lifetime]")
{
    const RDGTexture t0 = rdg.CreateTexture("t0"_sid, TexInfo());
    const RDGTexture t1 = rdg.CreateTexture("t1"_sid, TexInfo(640, 480));
    const RDGTexture t2 = rdg.CreateTexture("t2"_sid, TexInfo(320, 240));
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(t0);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(t0).WriteStorageImage(t1);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(t1).WriteStorageImage(t2);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(t2);
    Compile();
    CHECK(inspector.TextureFirstPass(t1) == 1);
    CHECK(inspector.TextureLastPass(t1) == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer lifetime tracked across passes", "[rdg][lifetime]")
{
    const RDGBuffer buf = rdg.CreateBuffer("buf"_sid, 1024);
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(buf);
    Compile();
    CHECK(inspector.BufferFirstPass(buf) == 0);
    CHECK(inspector.BufferLastPass(buf) == 1);
}

// ================================================================================
// Section 5: Auto-clear (into the barrier stream)
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: clear-value texture is cleared only in its first pass", "[rdg][autoclear]")
{
    VkClearValue cv{};
    cv.color = {{0.f, 0.f, 0.f, 1.f}};
    const RDGTexture clearTex = rdg.CreateTexture("clearTex"_sid, TexInfo(), cv);
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(clearTex);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(clearTex);
    Compile();
    CHECK(inspector.TextureHasClearInPass(clearTex, "pA"_sid));
    CHECK_FALSE(inspector.TextureHasClearInPass(clearTex, "pB"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: texture without a clear value is never auto-cleared", "[rdg][autoclear]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    Compile();
    CHECK_FALSE(inspector.TextureHasClearInPass(tex, "pA"_sid));
}

TEST_CASE_METHOD(RdgFixture, "RDG: clear-value texture first used in a later wave emits a pre-clear barrier", "[rdg][autoclear][barrier]")
{
    // producer occupies wave 0; the clear texture is first touched in wave 1.
    VkClearValue cv{};
    cv.color = {{1.f, 0.f, 0.f, 1.f}};
    const RDGTexture seed = rdg.CreateTexture("seed"_sid, TexInfo());
    const RDGTexture clearTex = rdg.CreateTexture("clearTex"_sid, TexInfo(640, 480), cv);
    rdg.AddPass("produce"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(seed);
    rdg.AddPass("consume"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged)
            .ReadStorageImage(seed).WriteStorageImage(clearTex);
    Compile();

    CHECK(inspector.PassWaveIndex("consume"_sid) == 1);
    CHECK(inspector.WavePreClearCount(1) >= 1);
    const VkImageMemoryBarrier2* preClear = inspector.FindPreClearBarrier(1, clearTex);
    REQUIRE(preClear != nullptr);
    CHECK(preClear->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(preClear->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CHECK(inspector.WaveRangesContiguous(1));
}

// ================================================================================
// Section 6: Physical resource aliasing
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: non-overlapping same-dim textures share one physical image", "[rdg][aliasing]")
{
    const RDGTexture texA = rdg.CreateTexture("texA"_sid, TexInfo());
    const RDGTexture texB = rdg.CreateTexture("texB"_sid, TexInfo(640, 480));
    const RDGTexture texC = rdg.CreateTexture("texC"_sid, TexInfo()); // same dims as texA
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(texA);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texA).WriteStorageImage(texB);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texB).WriteStorageImage(texC);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texC);
    Compile();
    CHECK(inspector.TexturesSharePhysical(texA, texC));
    CHECK(inspector.TextureImage(texA) == inspector.TextureImage(texC));
    CHECK_FALSE(inspector.TexturesSharePhysical(texA, texB));
}

TEST_CASE_METHOD(RdgFixture, "RDG: overlapping same-dim textures do not alias", "[rdg][aliasing]")
{
    const RDGTexture texA = rdg.CreateTexture("texA"_sid, TexInfo());
    const RDGTexture texB = rdg.CreateTexture("texB"_sid, TexInfo());
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(texA).WriteStorageImage(texB);
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(texA, texB));
    CHECK(inspector.PhysicalCount() >= 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: different-dimension textures never alias", "[rdg][aliasing]")
{
    const RDGTexture big = rdg.CreateTexture("big"_sid, TexInfo(1920, 1080));
    const RDGTexture small = rdg.CreateTexture("small"_sid, TexInfo(960, 540));
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(big);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(small);
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(big, small));
}

TEST_CASE_METHOD(RdgFixture, "RDG: imported textures are never aliased with transients", "[rdg][aliasing]")
{
    auto img = reinterpret_cast<VkImage>(0x1111ull);
    auto view = reinterpret_cast<VkImageView>(0x2222ull);
    const RDGTexture imported = rdg.ImportTexture("imported"_sid, img, view, TexInfo(), VK_IMAGE_USAGE_STORAGE_BIT,
                                                  VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    const RDGTexture transient = rdg.CreateTexture("transient"_sid, TexInfo());
    rdg.AddPass("p0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(imported);
    rdg.AddPass("p1"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(transient);
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(imported, transient));
    CHECK(inspector.PhysicalIsImported(inspector.TexturePhysicalIndex(imported)));
}

TEST_CASE_METHOD(RdgFixture, "RDG: aliased physical grows usage to the union of its tenants", "[rdg][aliasing]")
{
    // texA is storage-only; texC (same dims, non-overlapping life) is sampled+storage. The shared
    // physical must carry both usage bits.
    const RDGTexture texA = rdg.CreateTexture("texA"_sid, TexInfo());
    const RDGTexture texB = rdg.CreateTexture("texB"_sid, TexInfo(640, 480));
    const RDGTexture texC = rdg.CreateTexture("texC"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(texA);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texA).WriteStorageImage(texB);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texB).WriteStorageImage(texC);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(texC);
    Compile();
    REQUIRE(inspector.TexturesSharePhysical(texA, texC));
    const VkImageUsageFlags physUsage = inspector.PhysicalImageUsage(inspector.TexturePhysicalIndex(texA));
    CHECK((physUsage & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
    CHECK((physUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: viewport-scaled flag propagates onto the physical", "[rdg][aliasing]")
{
    const RDGTexture vp = rdg.CreateTexture("vp"_sid, TexInfo(), std::nullopt, /*bIsViewportScaled=*/true);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(vp);
    Compile();
    CHECK(inspector.PhysicalIsViewportScaled(inspector.TexturePhysicalIndex(vp)));
}

TEST_CASE_METHOD(RdgFixture, "RDG: a versioned texture cannot alias even with a free slot", "[rdg][aliasing]")
{
    // texA is versioned (bCanUseAliasedTexture=false); texC has a non-overlapping life and
    // identical dims but still must not reuse texA's physical.
    const RDGTexture texA = rdg.CreateVersionedTexture("texA"_sid, TexInfo(), 1, VersionSource::Fresh).Current();
    const RDGTexture texB = rdg.CreateTexture("texB"_sid, TexInfo(640, 480));
    const RDGTexture texC = rdg.CreateTexture("texC"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(texA);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texA).WriteStorageImage(texB);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texB).WriteStorageImage(texC);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(texC);
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(texA, texC));
}

TEST_CASE_METHOD(RdgFixture, "RDG: chain reuses head/tail into one physical, distinct middle", "[rdg][aliasing]")
{
    const RDGTexture a = rdg.CreateTexture("A"_sid, TexInfo());
    const RDGTexture b = rdg.CreateTexture("B"_sid, TexInfo(640, 480));
    const RDGTexture c = rdg.CreateTexture("C"_sid, TexInfo());
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(a);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(a).WriteStorageImage(b);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(b).WriteStorageImage(c);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(c);
    Compile();
    CHECK(inspector.TexturesSharePhysical(a, c));
    CHECK(inspector.PhysicalCount() == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: overlapping same-size buffers do not alias", "[rdg][aliasing]")
{
    const RDGBuffer bufA = rdg.CreateBuffer("bufA"_sid, 4096);
    const RDGBuffer bufB = rdg.CreateBuffer("bufB"_sid, 4096);
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(bufA);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(bufA).WriteBuffer(bufB);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(bufB);
    Compile();
    CHECK_FALSE(inspector.BuffersSharePhysical(bufA, bufB));
}

TEST_CASE_METHOD(RdgFixture, "RDG: non-overlapping same-size buffers alias", "[rdg][aliasing]")
{
    // bufA lives in waves 0-1; bufC lives in waves 2-3 (same size) -> alias.
    const RDGBuffer bufA = rdg.CreateBuffer("bufA"_sid, 4096);
    const RDGBuffer bufB = rdg.CreateBuffer("bufB"_sid, 2048);
    const RDGBuffer bufC = rdg.CreateBuffer("bufC"_sid, 4096);
    rdg.AddPass("pA"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(bufA);
    rdg.AddPass("pB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(bufA).WriteBuffer(bufB);
    rdg.AddPass("pC"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(bufB).WriteBuffer(bufC);
    rdg.AddPass("pD"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(bufC);
    Compile();
    CHECK(inspector.BuffersSharePhysical(bufA, bufC));
}

TEST_CASE_METHOD(RdgFixture, "RDG: bCanAlias=false buffer is always its own physical", "[rdg][aliasing]")
{
    const RDGBuffer noAlias = rdg.CreateBuffer("noAlias"_sid, 4096, false, /*bCanAlias=*/false);
    const RDGBuffer normal = rdg.CreateBuffer("normal"_sid, 4096);
    rdg.AddPass("p0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(noAlias);
    rdg.AddPass("p1"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(normal);
    Compile();
    CHECK_FALSE(inspector.BuffersSharePhysical(noAlias, normal));
    CHECK_FALSE(inspector.PhysicalCanAlias(inspector.BufferPhysicalIndex(noAlias)));
}

// ================================================================================
// Section 7: Barriers (PrecomputeBarriers output)
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: first storage write barrier transitions UNDEFINED to GENERAL", "[rdg][barrier]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, tex);
    REQUIRE(b != nullptr);
    CHECK(b->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(b->srcStageMask == VK_PIPELINE_STAGE_2_NONE);
    CHECK(b->srcAccessMask == VK_ACCESS_2_NONE);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(b->dstAccessMask == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: a later wave's barrier sources from the previous wave's flushed event", "[rdg][barrier]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    rdg.AddPass("r"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(tex);
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(1, tex);
    REQUIRE(b != nullptr);
    CHECK(b->oldLayout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(b->srcStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(b->srcAccessMask == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    CHECK(b->dstAccessMask == VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: color attachment barrier uses output stage and write|read access", "[rdg][barrier]")
{
    const RDGTexture color = rdg.CreateTexture("color"_sid, TexInfo());
    rdg.AddPass("draw"_sid, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, Render::RenderCategory::Untagged).WriteColorAttachment(color);
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, color);
    REQUIRE(b != nullptr);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(b->dstAccessMask == (VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT));
}

TEST_CASE_METHOD(RdgFixture, "RDG: depth write barrier uses fragment-test stages and read+write access", "[rdg][barrier]")
{
    // Depth writes are read-modify-write: the depth test reads the attachment even in write passes
    const RDGTexture depth = rdg.CreateTexture("depth"_sid, DepthTexInfo());
    rdg.AddPass("z"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).WriteDepthAttachment(depth);
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, depth);
    REQUIRE(b != nullptr);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK(b->dstStageMask == (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT));
    CHECK(b->dstAccessMask == (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
    CHECK((inspector.PhysicalAspect(inspector.TexturePhysicalIndex(depth)) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: read-only depth barrier carries read access only", "[rdg][barrier]")
{
    const RDGTexture depth = rdg.CreateTexture("depth"_sid, DepthTexInfo());
    rdg.AddPass("z"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).WriteDepthAttachment(depth);
    rdg.AddPass("read"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).ReadDepthAttachment(depth);
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(1, depth);
    REQUIRE(b != nullptr);
    CHECK(b->dstAccessMask == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
}

// ================================================================================
// Section 7b: Hi-Z / two-phase occlusion pipeline shape
// Regression net for the phase-2 depth re-write: depth is written as an attachment, sampled by the
// Hi-Z build, then written as an attachment AGAIN. The WAR edge and both layout transitions must hold.
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: depth sampled between two depth writes (Hi-Z shape)", "[rdg][barrier][hiz]")
{
    const RDGTexture depth = rdg.CreateTexture("depth"_sid, DepthTexInfo());
    const RDGTexture pyr = rdg.CreateTexture("pyr"_sid, Render::TextureInfo{VK_FORMAT_R32_SFLOAT, 512, 256, 1});
    rdg.AddPass("z1"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).WriteDepthAttachment(depth);
    rdg.AddPass("hiz"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(depth).WriteStorageImage(pyr);
    rdg.AddPass("z2"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).WriteDepthAttachment(depth);
    Compile();

    // The phase-2 depth write must wait for the Hi-Z read (WAR), not run alongside it
    CHECK(inspector.HasEdge("hiz"_sid, "z2"_sid));
    CHECK(inspector.PassComesBeforeInSort("hiz"_sid, "z2"_sid));

    const VkImageMemoryBarrier2* toSampled = inspector.FindImageBarrier(inspector.PassWaveIndex("hiz"_sid), depth);
    REQUIRE(toSampled != nullptr);
    CHECK(toSampled->oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK(toSampled->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK((toSampled->srcStageMask & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) != 0);
    CHECK((toSampled->srcAccessMask & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) != 0);
    CHECK(toSampled->dstStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(toSampled->dstAccessMask == VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    const VkImageMemoryBarrier2* backToDepth = inspector.FindImageBarrier(inspector.PassWaveIndex("z2"_sid), depth);
    REQUIRE(backToDepth != nullptr);
    CHECK(backToDepth->oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(backToDepth->newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK((backToDepth->srcStageMask & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) != 0);
    CHECK(backToDepth->dstAccessMask == (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Hi-Z chunk chain via ReadWriteImage barriers each hand-off", "[rdg][barrier][hiz]")
{
    const RDGTexture pyr = rdg.CreateTexture("pyr"_sid, Render::TextureInfo{VK_FORMAT_R32_SFLOAT, 512, 256, 1});
    rdg.AddPass("chunk0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(pyr);
    rdg.AddPass("chunk1"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadWriteImage(pyr);
    rdg.AddPass("cull"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(pyr);
    Compile();

    CHECK(inspector.HasEdge("chunk0"_sid, "chunk1"_sid));
    CHECK(inspector.HasEdge("chunk1"_sid, "cull"_sid));

    // chunk0's storage writes must be visible to chunk1 even though the layout stays GENERAL
    const VkImageMemoryBarrier2* handoff = inspector.FindImageBarrier(inspector.PassWaveIndex("chunk1"_sid), pyr);
    REQUIRE(handoff != nullptr);
    CHECK(handoff->oldLayout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(handoff->newLayout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK((handoff->srcAccessMask & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) != 0);
    CHECK(handoff->dstAccessMask == (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));

    const VkImageMemoryBarrier2* toSampled = inspector.FindImageBarrier(inspector.PassWaveIndex("cull"_sid), pyr);
    REQUIRE(toSampled != nullptr);
    CHECK(toSampled->oldLayout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(toSampled->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK((toSampled->srcAccessMask & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) != 0);
    CHECK(toSampled->dstAccessMask == VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: two-phase occlusion mini-pipeline schedules the full depth ping-pong", "[rdg][barrier][hiz][e2e]")
{
    const RDGTexture depth = rdg.CreateTexture("depth"_sid, DepthTexInfo());
    const RDGTexture pyr = rdg.CreateTexture("pyr"_sid, Render::TextureInfo{VK_FORMAT_R32_SFLOAT, 512, 256, 6});
    const RDGBuffer vis = rdg.CreateBuffer("vis"_sid, 4096);

    rdg.AddPass("z1"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).WriteDepthAttachment(depth);
    rdg.AddPass("hiz0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(depth).WriteStorageImage(pyr);
    rdg.AddPass("hiz1"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadWriteImage(pyr);
    rdg.AddPass("cull"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(pyr).WriteBuffer(vis);
    rdg.AddPass("z2"_sid, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, Render::RenderCategory::Untagged).WriteDepthAttachment(depth).ReadBuffer(vis);
    rdg.AddPass("light"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(depth);
    Compile();

    CHECK(inspector.PassComesBeforeInSort("z1"_sid, "hiz0"_sid));
    CHECK(inspector.PassComesBeforeInSort("hiz0"_sid, "hiz1"_sid));
    CHECK(inspector.PassComesBeforeInSort("hiz1"_sid, "cull"_sid));
    CHECK(inspector.PassComesBeforeInSort("cull"_sid, "z2"_sid));
    CHECK(inspector.PassComesBeforeInSort("z2"_sid, "light"_sid));

    // Depth transitions: attachment -> sampled (hiz0), sampled -> attachment (z2), attachment -> sampled (light)
    const VkImageMemoryBarrier2* d1 = inspector.FindImageBarrier(inspector.PassWaveIndex("hiz0"_sid), depth);
    REQUIRE(d1 != nullptr);
    CHECK(d1->oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK(d1->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const VkImageMemoryBarrier2* d2 = inspector.FindImageBarrier(inspector.PassWaveIndex("z2"_sid), depth);
    REQUIRE(d2 != nullptr);
    CHECK(d2->oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(d2->newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    const VkImageMemoryBarrier2* d3 = inspector.FindImageBarrier(inspector.PassWaveIndex("light"_sid), depth);
    REQUIRE(d3 != nullptr);
    CHECK(d3->oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK(d3->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK((d3->srcAccessMask & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: blit read/write barriers use BLIT stage and TRANSFER layouts", "[rdg][barrier]")
{
    const RDGTexture src = rdg.CreateTexture("src"_sid, TexInfo());
    const RDGTexture dst = rdg.CreateTexture("dst"_sid, TexInfo(640, 480));
    rdg.AddPass("seed"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(src);
    rdg.AddPass("blit"_sid, VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged).ReadBlitImage(src).WriteBlitImage(dst);
    Compile();
    const VkImageMemoryBarrier2* read = inspector.FindImageBarrier(1, src);
    const VkImageMemoryBarrier2* write = inspector.FindImageBarrier(1, dst);
    REQUIRE(read != nullptr);
    REQUIRE(write != nullptr);
    CHECK(read->newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    CHECK(read->dstStageMask == VK_PIPELINE_STAGE_2_BLIT_BIT);
    CHECK(read->dstAccessMask == VK_ACCESS_2_TRANSFER_READ_BIT);
    CHECK(write->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CHECK(write->dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer RAW barrier sources writer, targets reader", "[rdg][barrier]")
{
    const RDGBuffer buf = rdg.CreateBuffer("buf"_sid, 1024);
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);
    rdg.AddPass("r"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(buf);
    Compile();
    const VkBufferMemoryBarrier2* b = inspector.FindBufferBarrier(1, buf);
    REQUIRE(b != nullptr);
    CHECK(b->srcStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(b->srcAccessMask == VK_ACCESS_2_SHADER_WRITE_BIT);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(b->dstAccessMask == VK_ACCESS_2_SHADER_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: index buffer barrier uses INDEX_INPUT stage and INDEX_READ access", "[rdg][barrier]")
{
    const RDGBuffer idx = rdg.CreateBuffer("idx"_sid, 1024);
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(idx);
    rdg.AddPass("draw"_sid, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, Render::RenderCategory::Untagged).ReadIndexBuffer(idx);
    Compile();
    const VkBufferMemoryBarrier2* b = inspector.FindBufferBarrier(1, idx);
    REQUIRE(b != nullptr);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
    CHECK(b->dstAccessMask == VK_ACCESS_2_INDEX_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: indirect buffer barrier carries draw-indirect and shader-read", "[rdg][barrier]")
{
    const RDGBuffer indirect = rdg.CreateBuffer("indirect"_sid, 1024);
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(indirect);
    rdg.AddPass("draw"_sid, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, Render::RenderCategory::Untagged).ReadIndirectBuffer(indirect);
    Compile();
    const VkBufferMemoryBarrier2* b = inspector.FindBufferBarrier(1, indirect);
    REQUIRE(b != nullptr);
    CHECK((b->dstStageMask & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) != 0);
    CHECK(b->dstAccessMask == (VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT));
}

TEST_CASE_METHOD(RdgFixture, "RDG: same image read two ways in one wave merges into a single barrier", "[rdg][barrier]")
{
    // An imported texture (non-null image) sampled by two independent same-wave passes: the barrier
    // dedup OR-merges their destination stages into one image barrier.
    auto img = reinterpret_cast<VkImage>(0xABCD01ull);
    auto view = reinterpret_cast<VkImageView>(0xABCD02ull);
    const RDGTexture shared = rdg.ImportTexture("shared"_sid, img, view, TexInfo(), VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rdg.AddPass("readA"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(shared);
    rdg.AddPass("readB"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(shared);
    Compile();
    CHECK(inspector.PassWaveIndex("readA"_sid) == 0);
    CHECK(inspector.PassWaveIndex("readB"_sid) == 0);
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, shared);
    REQUIRE(b != nullptr);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(b->dstStageMask == (VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    CHECK(b->dstAccessMask == VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: no-barrier imported buffer emits no buffer barriers", "[rdg][barrier]")
{
    auto buf = reinterpret_cast<VkBuffer>(0xB0FFEEull);
    const RDGBuffer ext = rdg.ImportBufferNoBarrier("ext"_sid, buf, 0x1000, {1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    rdg.AddPass("w"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(ext);
    rdg.AddPass("r"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadBuffer(ext);
    Compile();
    CHECK(inspector.PhysicalDisableBarriers(inspector.BufferPhysicalIndex(ext)));
    CHECK(inspector.TotalBufferBarriers() == 0);
}

// ================================================================================
// Section 8: Imported resources
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: ImportTexture records initial state, aspect and final layout", "[rdg][import]")
{
    auto img = reinterpret_cast<VkImage>(0xD00D01ull);
    auto view = reinterpret_cast<VkImageView>(0xD00D02ull);
    const RDGTexture imp = rdg.ImportTexture("imp"_sid, img, view, DepthTexInfo(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const uint32_t physIdx = inspector.TexturePhysicalIndex(imp);
    CHECK(inspector.PhysicalIsImported(physIdx));
    CHECK(inspector.PhysicalImage(physIdx) == img);
    CHECK(inspector.TextureLayout(imp) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK(inspector.TextureFinalLayout(imp) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK((inspector.PhysicalAspect(physIdx) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: re-importing the same image next frame reuses its physical", "[rdg][import]")
{
    auto img = reinterpret_cast<VkImage>(0xE0E0E0ull);
    auto view = reinterpret_cast<VkImageView>(0xE0E0E1ull);
    RDGTexture imp = rdg.ImportTexture("imp"_sid, img, view, TexInfo(), VK_IMAGE_USAGE_STORAGE_BIT,
                                       VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(imp);
    Compile(0);
    const size_t physCount = inspector.PhysicalCount();

    NextFrame(0, 1);
    imp = rdg.ImportTexture("imp"_sid, img, view, TexInfo(), VK_IMAGE_USAGE_STORAGE_BIT,
                            VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(imp);
    Compile(1);
    CHECK(inspector.PhysicalCount() == physCount);
    CHECK(inspector.PhysicalImage(inspector.TexturePhysicalIndex(imp)) == img);
}

TEST_CASE_METHOD(RdgFixture, "RDG: ImportBuffer with device-address usage retrieves a deterministic address", "[rdg][import]")
{
    auto buf = reinterpret_cast<VkBuffer>(0xF00001ull);
    PipelineEvent state{VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT};
    const RDGBuffer ext = rdg.ImportBuffer("ext"_sid, buf, 0, {2048, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, state);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(ext);
    Compile();

    const uint32_t physIdx = inspector.BufferPhysicalIndex(ext);
    CHECK(inspector.PhysicalAddressRetrieved(physIdx));
    auto val = reinterpret_cast<uint64_t>(buf);
    const VkDeviceAddress expected = Hash(&val, sizeof(val));
    CHECK(rdg.GetBufferAddress(ext) == expected);
}

// ================================================================================
// Section 9: Multi-frame reset, eviction & carryover
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: Reset removes previously registered logical resources", "[rdg][reset]")
{
    rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.CreateBuffer("buf"_sid, 1024);
    CHECK(rdg.FindTexture("tex"_sid).IsValid());
    CHECK(rdg.FindBuffer("buf"_sid).IsValid());
    NextFrame();
    CHECK_FALSE(rdg.FindTexture("tex"_sid).IsValid());
    CHECK_FALSE(rdg.FindBuffer("buf"_sid).IsValid());
}

TEST_CASE_METHOD(RdgFixture, "RDG: identical consecutive frames reuse physicals without growth", "[rdg][reset]")
{
    auto build = [&]() {
        const RDGTexture t = rdg.CreateTexture("t"_sid, TexInfo());
        rdg.AddPass("p0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(t);
        rdg.AddPass("p1"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(t);
    };
    build();
    Compile(0);
    const size_t phys0 = inspector.PhysicalCount();

    NextFrame(0, 1);
    build();
    Compile(1);
    CHECK(inspector.PhysicalCount() == phys0);
    CHECK(inspector.PassWaveIndex("p0"_sid) == 0);
    CHECK(inspector.PassWaveIndex("p1"_sid) == 1);
}

TEST_CASE_METHOD(RdgFixture, "RDG: a physical unused beyond maxFramesUnused is evicted", "[rdg][reset]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);
    Compile(0);
    CHECK(inspector.PhysicalCount() >= 1);

    rdg.FrameStartReset(0, 1, 1); // frame 1, maxUnused=1: nothing uses the texture this frame
    Compile(1);
    rdg.FrameStartReset(0, 2, 1); // frame 2: unused for one full frame -> evicted
    CHECK(inspector.PhysicalCount() == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: versioned texture rotates when the bare name is produced", "[rdg][reset][versioned]")
{
    RDGTextureRing history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::Fresh);
    CHECK_FALSE(rdg.ResourceHasVersion("history"_sid, 1));
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(history.Current());
    Compile(0);
    const VkImage frame0 = inspector.TextureImage(history.Current());

    NextFrame(0, 1);
    history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::Fresh);
    CHECK(rdg.ResourceHasVersion("history"_sid, 1));
    const RDGTexture prev = history.Version(1);
    CHECK(inspector.TextureImage(prev) == frame0);
    CHECK_FALSE(inspector.PhysicalCanAlias(inspector.TexturePhysicalIndex(prev)));
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadStorageImage(prev).WriteStorageImage(history.Current());
    Compile(1);
    CHECK(inspector.TextureImage(history.Current()) != frame0);
    CHECK(inspector.TextureImage(prev) == frame0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: NoShiftReadOnly binds the newest version to the bare name and does not shift", "[rdg][reset][versioned]")
{
    RDGTextureRing history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::Fresh);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(history.Current());
    Compile(0);
    const VkImage frame0 = inspector.TextureImage(history.Current());

    NextFrame(0, 1);
    history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::NoShiftReadOnly);
    CHECK(rdg.ResourceHasVersion("history"_sid, 0));
    CHECK_FALSE(rdg.ResourceHasVersion("history"_sid, 1));
    CHECK(inspector.TextureImage(history.Current()) == frame0);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadSampledImage(history.Current());
    Compile(1);

    NextFrame(0, 2);
    history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::NoShiftReadOnly);
    CHECK(inspector.TextureImage(history.Current()) == frame0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: versioned texture with no history has no bare name until produced", "[rdg][reset][versioned]")
{
    RDGTextureRing history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::Emplaced, false, VK_IMAGE_USAGE_SAMPLED_BIT);
    CHECK_FALSE(history.Current().IsValid());
    CHECK_FALSE(rdg.ResourceHasVersion("history"_sid, 1));
    const RDGTexture current = rdg.CreateTexture("current"_sid, TexInfo());
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(current);
    rdg.EmplaceVersion(history, current);
    Compile(0);
    const VkImage adopted = inspector.TextureImage(current);

    NextFrame(0, 1);
    history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::Emplaced, false, VK_IMAGE_USAGE_SAMPLED_BIT);
    CHECK_FALSE(history.Current().IsValid());
    CHECK(rdg.ResourceHasVersion("history"_sid, 1));
    CHECK(inspector.TextureImage(history.Version(1)) == adopted);
    CHECK(inspector.TextureAccumulatedUsage(history.Version(1)) & VK_IMAGE_USAGE_SAMPLED_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: NoShiftReadWrite depth 0 keeps one physical across frames", "[rdg][reset][versioned]")
{
    CHECK_FALSE(rdg.ResourceHasVersion("accum"_sid, 0));
    RDGBufferRing accum = rdg.CreateVersionedBuffer("accum"_sid, 4096, 0, VersionSource::Fresh);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadWriteBuffer(accum.Current());
    Compile(0);
    const VkBuffer frame0 = inspector.BufferHandle(accum.Current());

    NextFrame(0, 1);
    accum = rdg.CreateVersionedBuffer("accum"_sid, 4096, 0, VersionSource::NoShiftReadWrite);
    CHECK(rdg.ResourceHasVersion("accum"_sid, 0));
    CHECK(inspector.BufferHandle(accum.Current()) == frame0);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).ReadWriteBuffer(accum.Current());
    Compile(1);
    CHECK(inspector.BufferHandle(accum.Current()) == frame0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: depth-3 versioned buffer keeps three frames of history", "[rdg][reset][versioned]")
{
    VkBuffer handles[4]{};
    RDGBufferRing touch{};
    for (uint32_t frame = 0; frame < 4; ++frame) {
        if (frame > 0) { NextFrame(0, frame); }
        touch = rdg.CreateVersionedBuffer("touch"_sid, 4096, 3, VersionSource::Fresh);
        rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(touch.Current());
        CHECK(rdg.ResourceHasVersion("touch"_sid, 3) == (frame >= 3));
        Compile(frame);
        handles[frame] = inspector.BufferHandle(touch.Current());
    }
    CHECK(inspector.BufferHandle(touch.Version(1)) == handles[2]);
    CHECK(inspector.BufferHandle(touch.Version(2)) == handles[1]);
    CHECK(inspector.BufferHandle(touch.Version(3)) == handles[0]);
    CHECK(handles[3] != handles[0]);
}

TEST_CASE_METHOD(RdgFixture, "RDG: a versioned resource not declared for a frame is dropped", "[rdg][reset][versioned]")
{
    const RDGTextureRing history = rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::Fresh);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(history.Current());
    Compile(0);
    CHECK(inspector.RingCount() == 1);

    NextFrame(0, 1);
    Compile(1);

    NextFrame(0, 2);
    CHECK(inspector.RingCount() == 0);
    rdg.CreateVersionedTexture("history"_sid, TexInfo(), 1, VersionSource::NoShiftReadOnly);
    CHECK_FALSE(rdg.ResourceHasVersion("history"_sid, 1));
}

TEST_CASE_METHOD(RdgFixture, "RDG: viewport-scaled physical is evicted on InvalidateAllViewportAssociated", "[rdg][reset][viewport]")
{
    const RDGTexture vp = rdg.CreateTexture("vp"_sid, TexInfo(), std::nullopt, true);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(vp);
    Compile(0);
    CHECK(inspector.PhysicalCount() == 1);
    rdg.InvalidateAllViewportAssociated();
    rdg.FrameStartReset(0, 1, 100);
    CHECK(inspector.PhysicalCount() == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: non-viewport physical survives InvalidateAllViewportAssociated", "[rdg][reset][viewport]")
{
    const RDGTexture vp = rdg.CreateTexture("vp"_sid, TexInfo(), std::nullopt, true);
    const RDGTexture staticTex = rdg.CreateTexture("static"_sid, TexInfo(64, 64));
    rdg.AddPass("p0"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(vp);
    rdg.AddPass("p1"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(staticTex);
    Compile(0);
    rdg.InvalidateAllViewportAssociated();
    rdg.FrameStartReset(0, 1, 100);
    CHECK(inspector.PhysicalCount() == 1);
    CHECK_FALSE(inspector.PhysicalIsViewportScaled(0));
}

TEST_CASE_METHOD(RdgFixture, "RDG: swapchain physical is removed on InvalidateAllSwapchainAssociated", "[rdg][reset][swapchain]")
{
    auto img = reinterpret_cast<VkImage>(0x5C0001ull);
    auto view = reinterpret_cast<VkImageView>(0x5C0002ull);
    const RDGTexture swap = rdg.ImportTexture("swap"_sid, img, view, TexInfo(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                              VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true);
    rdg.AddPass("p"_sid, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, Render::RenderCategory::Untagged).WriteColorAttachment(swap);
    Compile(0);
    CHECK(inspector.PhysicalCount() == 1);
    rdg.InvalidateAllSwapchainAssociated();
    rdg.FrameStartReset(0, 1, 100);
    CHECK(inspector.PhysicalCount() == 0);
}

// ================================================================================
// Section 10: Transient uploader & readback
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: AllocateTransient hands out sequential non-overlapping offsets", "[rdg][uploader]")
{
    const UploadAllocation a0 = rdg.AllocateTransient(256);
    const UploadAllocation a1 = rdg.AllocateTransient(256);
    CHECK(a0.ptr != nullptr);
    CHECK(a1.ptr != nullptr);
    CHECK(a1.offset >= a0.offset + 256);
}

TEST_CASE_METHOD(RdgFixture, "RDG: transient arena grows on exhaustion and preserves data", "[rdg][uploader]")
{
    const UploadAllocation a0 = rdg.AllocateTransient(64);
    REQUIRE(a0.ptr != nullptr);
    unsigned char pattern[64];
    for (int i = 0; i < 64; ++i) { pattern[i] = static_cast<unsigned char>(i + 1); }
    std::memcpy(a0.ptr, pattern, sizeof(pattern));

    VkBuffer before = rdg.GetTransientUploadBuffer();
    const UploadAllocation a1 = rdg.AllocateTransient(2 * 1024 * 1024); // exceeds the 1MB base
    VkBuffer after = rdg.GetTransientUploadBuffer();

    CHECK(before != after); // a new, larger backing buffer was created
    char* newBase = static_cast<char*>(a1.ptr) - a1.offset;
    CHECK(std::memcmp(newBase, pattern, sizeof(pattern)) == 0); // bytes carried across the grow
}

TEST_CASE_METHOD(RdgFixture, "RDG: Reset rewinds the transient allocator", "[rdg][uploader]")
{
    rdg.AllocateTransient(4096);
    NextFrame(0, 1);
    const UploadAllocation a = rdg.AllocateTransient(256);
    CHECK(a.offset == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: readback buffer and mapping are available", "[rdg][uploader]")
{
    CHECK(rdg.GetReadback() != VK_NULL_HANDLE);
    CHECK(rdg.GetReadbackData() != nullptr);
}

// ================================================================================
// Section 11: Execute
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: Execute runs passes and descriptor indices resolve inside the lambda", "[rdg][execute]")
{
    const RDGTexture tex = rdg.CreateTexture("tex"_sid, TexInfo());
    rdg.AddPass("write"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteStorageImage(tex);

    bool ran = false;
    uint32_t resolvedIndex = Core::INVALID_HANDLE_INDEX;
    rdg.AddPass("read"_sid, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Untagged)
            .ReadSampledImage(tex)
            .Execute([&ran, &resolvedIndex, tex](VkCommandBuffer, VulkanContext*, RenderGraph& graph) {
                ran = true;
                resolvedIndex = graph.GetSampledImageViewDescriptorIndex(tex);
            });

    Compile();
    rdg.Execute(reinterpret_cast<VkCommandBuffer>(uintptr_t{2}), reinterpret_cast<VkCommandBuffer>(uintptr_t{1}));

    CHECK(ran);
    const uint32_t physIdx = inspector.TexturePhysicalIndex(tex);
    CHECK(inspector.PhysicalSampledDescriptorValid(physIdx));
    CHECK(resolvedIndex != Core::INVALID_HANDLE_INDEX);
    CHECK(resolvedIndex == inspector.PhysicalSampledDescriptorIndex(physIdx));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Execute resolves buffer device address inside the lambda", "[rdg][execute]")
{
    const RDGBuffer buf = rdg.CreateBuffer("buf"_sid, 1024);
    rdg.AddPass("write"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged).WriteBuffer(buf);

    bool ran = false;
    VkDeviceAddress resolvedAddress = 0;
    rdg.AddPass("read"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged)
            .ReadBuffer(buf)
            .Execute([&ran, &resolvedAddress, buf](VkCommandBuffer, VulkanContext*, RenderGraph& graph) {
                ran = true;
                resolvedAddress = graph.GetBufferAddress(buf);
            });

    Compile();
    rdg.Execute(reinterpret_cast<VkCommandBuffer>(uintptr_t{2}), reinterpret_cast<VkCommandBuffer>(uintptr_t{1}));

    CHECK(ran);
    CHECK(resolvedAddress != 0);
    const uint32_t physIdx = inspector.BufferPhysicalIndex(buf);
    CHECK(inspector.PhysicalAddressRetrieved(physIdx));
    CHECK(resolvedAddress == inspector.PhysicalBufferAddress(physIdx));
}

// ================================================================================
// Section 10: Raytracing buffer declarations (WriteTLASBuffer / ReadTLASBuffer / WriteScratchBuffer)
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: TLAS and scratch buffer declarations accumulate correct usage flags", "[rdg][usage][raytracing]")
{
    const RDGBuffer instanceBuf = rdg.CreateBuffer("instanceBuf"_sid, 4096);
    const RDGBuffer tlasBuf = rdg.CreateBuffer("tlasBuf"_sid, 65536);
    const RDGBuffer scratchBuf = rdg.CreateBuffer("scratchBuf"_sid, 65536);

    rdg.AddPass("uploadInstances"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged)
            .WriteTransferBuffer(instanceBuf);
    rdg.AddPass("buildTLAS"_sid, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, Render::RenderCategory::Untagged)
            .ReadTransferBuffer(instanceBuf)
            .WriteTLASBuffer(tlasBuf)
            .WriteScratchBuffer(scratchBuf);
    rdg.AddPass("rtShade"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged)
            .ReadTLASBuffer(tlasBuf);

    rdg.AccumulateUsage();

    // Instance buffer: transfer destination from upload, transfer source for build read
    CHECK((inspector.BufferAccumulatedUsage(instanceBuf) & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(instanceBuf) & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0);

    // TLAS buffer: must have AS storage and device address
    const VkBufferUsageFlags tlasUsage = inspector.BufferAccumulatedUsage(tlasBuf);
    CHECK((tlasUsage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) != 0);
    CHECK((tlasUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0);

    // Scratch buffer: must have storage and device address
    const VkBufferUsageFlags scratchUsage = inspector.BufferAccumulatedUsage(scratchBuf);
    CHECK((scratchUsage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0);
    CHECK((scratchUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: TLAS build pass emits correct barriers and dependency edges", "[rdg][barrier][raytracing]")
{
    const RDGBuffer instanceBuf = rdg.CreateBuffer("instanceBuf"_sid, 4096);
    const RDGBuffer tlasBuf = rdg.CreateBuffer("tlasBuf"_sid, 65536);
    const RDGBuffer scratchBuf = rdg.CreateBuffer("scratchBuf"_sid, 65536);

    rdg.AddPass("uploadInstances"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged)
            .WriteTransferBuffer(instanceBuf);
    rdg.AddPass("buildTLAS"_sid, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, Render::RenderCategory::Untagged)
            .ReadTransferBuffer(instanceBuf)
            .WriteTLASBuffer(tlasBuf)
            .WriteScratchBuffer(scratchBuf);
    rdg.AddPass("rtShade"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged)
            .ReadTLASBuffer(tlasBuf);

    Compile();

    // Scheduling: upload -> build -> shade must be sequential waves
    CHECK(inspector.PassWaveIndex("uploadInstances"_sid) < inspector.PassWaveIndex("buildTLAS"_sid));
    CHECK(inspector.PassWaveIndex("buildTLAS"_sid) < inspector.PassWaveIndex("rtShade"_sid));

    // Dependency edges
    CHECK(inspector.HasEdge("uploadInstances"_sid, "buildTLAS"_sid));
    CHECK(inspector.HasEdge("buildTLAS"_sid, "rtShade"_sid));
    CHECK_FALSE(inspector.HasEdge("uploadInstances"_sid, "rtShade"_sid));

    // buildTLAS wave: TLAS buffer barrier must use AS_BUILD stage and AS_WRITE access
    const uint32_t buildWave = inspector.PassWaveIndex("buildTLAS"_sid);
    const VkBufferMemoryBarrier2* tlasBarrier = inspector.FindBufferBarrier(buildWave, tlasBuf);
    REQUIRE(tlasBarrier != nullptr);
    CHECK((tlasBarrier->dstStageMask & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) != 0);
    CHECK((tlasBarrier->dstAccessMask & VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR) != 0);

    // scratch buffer barrier: same stage/access as TLAS write
    const VkBufferMemoryBarrier2* scratchBarrier = inspector.FindBufferBarrier(buildWave, scratchBuf);
    REQUIRE(scratchBarrier != nullptr);
    CHECK((scratchBarrier->dstStageMask & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) != 0);
    CHECK((scratchBarrier->dstAccessMask & VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR) != 0);

    // rtShade wave: TLAS read barrier must use AS_READ access
    const uint32_t shadeWave = inspector.PassWaveIndex("rtShade"_sid);
    const VkBufferMemoryBarrier2* tlasReadBarrier = inspector.FindBufferBarrier(shadeWave, tlasBuf);
    REQUIRE(tlasReadBarrier != nullptr);
    CHECK((tlasReadBarrier->dstAccessMask & VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR) != 0);
    CHECK((tlasReadBarrier->srcStageMask & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) != 0);
}
