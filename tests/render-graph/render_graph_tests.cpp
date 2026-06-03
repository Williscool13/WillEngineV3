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

#include "core/hash/fnv_1_a.h"
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
    [[nodiscard]] VkImageUsageFlags TextureAccumulatedUsage(StringID id) const
    {
        for (auto& tex : rdg.textures) {
            if (tex.textureId == id) { return tex.accumulatedUsage; }
        }
        return 0;
    }

    [[nodiscard]] VkBufferUsageFlags BufferAccumulatedUsage(StringID id) const
    {
        for (auto& buf : rdg.buffers) {
            if (buf.bufferId == id) { return buf.accumulatedUsage; }
        }
        return 0;
    }

    // ---- Lifetimes ------------------------------------------------------------------------
    [[nodiscard]] uint32_t TextureFirstPass(StringID id) const
    {
        const TextureResource* t = Tex(id);
        return t ? t->firstPass : UINT32_MAX;
    }

    [[nodiscard]] uint32_t TextureLastPass(StringID id) const
    {
        const TextureResource* t = Tex(id);
        return t ? t->lastPass : UINT32_MAX;
    }

    [[nodiscard]] uint32_t BufferFirstPass(StringID id) const
    {
        const BufferResource* b = Buf(id);
        return b ? b->firstPass : UINT32_MAX;
    }

    [[nodiscard]] uint32_t BufferLastPass(StringID id) const
    {
        const BufferResource* b = Buf(id);
        return b ? b->lastPass : UINT32_MAX;
    }

    // ---- Physical resources ---------------------------------------------------------------
    [[nodiscard]] size_t PhysicalCount() const { return rdg.physicalResources.Size(); }

    [[nodiscard]] uint32_t TexturePhysicalIndex(StringID id) const
    {
        const TextureResource* t = Tex(id);
        return t ? t->physicalIndex : UINT32_MAX;
    }

    [[nodiscard]] uint32_t BufferPhysicalIndex(StringID id) const
    {
        const BufferResource* b = Buf(id);
        return b ? b->physicalIndex : UINT32_MAX;
    }

    [[nodiscard]] bool TexturesSharePhysical(StringID a, StringID b) const
    {
        const uint32_t ia = TexturePhysicalIndex(a);
        const uint32_t ib = TexturePhysicalIndex(b);
        return ia != UINT32_MAX && ia == ib;
    }

    [[nodiscard]] bool BuffersSharePhysical(StringID a, StringID b) const
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

    [[nodiscard]] VkImage TextureImage(StringID id) const { return PhysicalImage(TexturePhysicalIndex(id)); }
    [[nodiscard]] VkBuffer BufferHandle(StringID id) const { return PhysicalBuffer(BufferPhysicalIndex(id)); }

    // ---- State ----------------------------------------------------------------------------
    [[nodiscard]] VkImageLayout TextureLayout(StringID id) const
    {
        const TextureResource* t = Tex(id);
        return t ? t->layout : VK_IMAGE_LAYOUT_UNDEFINED;
    }

    [[nodiscard]] VkImageLayout TextureFinalLayout(StringID id) const
    {
        const TextureResource* t = Tex(id);
        return t ? t->finalLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    }

    // ---- Auto-clear -----------------------------------------------------------------------
    [[nodiscard]] bool TextureHasClearInPass(StringID texId, StringID passId) const
    {
        const TextureResource* t = Tex(texId);
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

    // ---- Carryover ------------------------------------------------------------------------
    [[nodiscard]] size_t TextureCarryoverCount() const { return rdg.textureCarryovers.Size(); }
    [[nodiscard]] size_t BufferCarryoverCount() const { return rdg.bufferCarryovers.Size(); }

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

    [[nodiscard]] const VkImageMemoryBarrier2* FindImageBarrier(uint32_t wave, StringID texId) const
    {
        VkImage img = TextureImage(texId);
        const auto& r = rdg.compiledWaveRanges[wave];
        for (uint32_t i = 0; i < r.imageCount; ++i) {
            const VkImageMemoryBarrier2& b = rdg.compiledImageBarriers[r.imageStart + i];
            if (b.image == img) { return &b; }
        }
        return nullptr;
    }

    [[nodiscard]] const VkImageMemoryBarrier2* FindPreClearBarrier(uint32_t wave, StringID texId) const
    {
        VkImage img = TextureImage(texId);
        const auto& r = rdg.compiledWaveRanges[wave];
        for (uint32_t i = 0; i < r.preClearImageCount; ++i) {
            const VkImageMemoryBarrier2& b = rdg.compiledImageBarriers[r.preClearImageStart + i];
            if (b.image == img) { return &b; }
        }
        return nullptr;
    }

    [[nodiscard]] const VkBufferMemoryBarrier2* FindBufferBarrier(uint32_t wave, StringID bufId) const
    {
        VkBuffer buf = BufferHandle(bufId);
        const auto& r = rdg.compiledWaveRanges[wave];
        for (uint32_t i = 0; i < r.bufferCount; ++i) {
            const VkBufferMemoryBarrier2& b = rdg.compiledBufferBarriers[r.bufferStart + i];
            if (b.buffer == buf) { return &b; }
        }
        return nullptr;
    }

private:
    [[nodiscard]] bool Valid(uint32_t i) const { return i < rdg.physicalResources.Size(); }

    [[nodiscard]] const TextureResource* Tex(StringID id) const
    {
        for (auto& tex : rdg.textures) {
            if (tex.textureId == id) { return &tex; }
        }
        return nullptr;
    }

    [[nodiscard]] const BufferResource* Buf(StringID id) const
    {
        for (auto& buf : rdg.buffers) {
            if (buf.bufferId == id) { return &buf; }
        }
        return nullptr;
    }

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
        return fnv1a64(reinterpret_cast<const uint8_t*>(&val), sizeof(val));
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
        alloc.Init(tlsfPool.get(), TLSF_SIZE, false);
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
        rdg.Reset(0, 0, 100);
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
    void NextFrame(uint32_t frameIdx = 0, uint64_t frame = 1, uint64_t maxUnused = 100) { rdg.Reset(frameIdx, frame, maxUnused); }
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

    rdg.ImportTexture(SID("swap"), swapImg, swapView, TexInfo(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true);
    rdg.CreateTexture(SID("depth"), DepthTexInfo(), depthClear);
    rdg.CreateTexture(SID("gbuffer"), TexInfo());
    rdg.CreateTexture(SID("hdr"), TexInfo());
    rdg.CreateBuffer(SID("drawBuf"), 4096);
    rdg.CreateBuffer(SID("indirectBuf"), 4096);

    rdg.AddPass(SID("cull"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .WriteBuffer(SID("drawBuf"))
            .WriteBuffer(SID("indirectBuf"));
    rdg.AddPass(SID("depthPrepass"), VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT)
            .WriteDepthAttachment(SID("depth"));
    rdg.AddPass(SID("gbuffer"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
            .WriteColorAttachment(SID("gbuffer"))
            .ReadDepthAttachment(SID("depth"))
            .ReadIndirectBuffer(SID("indirectBuf"))
            .ReadBuffer(SID("drawBuf"));
    rdg.AddPass(SID("lighting"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .ReadSampledImage(SID("gbuffer"))
            .WriteStorageImage(SID("hdr"));
    rdg.AddPass(SID("post"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
            .ReadSampledImage(SID("hdr"))
            .WriteColorAttachment(SID("swap"));

    Compile();

    // Schedule
    CHECK(inspector.SortedPassCount() == 5);
    CHECK(inspector.WaveCount() == 4);
    CHECK(inspector.PassWaveIndex(SID("cull")) == 0);
    CHECK(inspector.PassWaveIndex(SID("depthPrepass")) == 0);
    CHECK(inspector.PassWaveIndex(SID("gbuffer")) == 1);
    CHECK(inspector.PassWaveIndex(SID("lighting")) == 2);
    CHECK(inspector.PassWaveIndex(SID("post")) == 3);

    // Dependency edges
    CHECK(inspector.HasEdge(SID("cull"), SID("gbuffer")));
    CHECK(inspector.HasEdge(SID("depthPrepass"), SID("gbuffer")));
    CHECK(inspector.HasEdge(SID("gbuffer"), SID("lighting")));
    CHECK(inspector.HasEdge(SID("lighting"), SID("post")));
    CHECK_FALSE(inspector.HasEdge(SID("cull"), SID("depthPrepass")));

    // Usage
    CHECK((inspector.BufferAccumulatedUsage(SID("drawBuf")) & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(SID("indirectBuf")) & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(SID("depth")) & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(SID("depth")) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0); // clear value
    CHECK((inspector.TextureAccumulatedUsage(SID("gbuffer")) & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(SID("gbuffer")) & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(SID("hdr")) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);

    // Imported swapchain: separate physical, present final layout stored.
    CHECK(inspector.PhysicalIsImported(inspector.TexturePhysicalIndex(SID("swap"))));
    CHECK(inspector.PhysicalIsSwapchain(inspector.TexturePhysicalIndex(SID("swap"))));
    CHECK(inspector.TextureFinalLayout(SID("swap")) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // gbuffer (waves 1-2) and hdr (waves 2-3) overlap at wave 2 -> cannot alias.
    CHECK_FALSE(inspector.TexturesSharePhysical(SID("gbuffer"), SID("hdr")));
    CHECK(inspector.PhysicalCount() >= 6);

    // Auto-clear: depth is cleared in its first pass (depthPrepass, wave 0) and gets a pre-clear barrier.
    CHECK(inspector.TextureHasClearInPass(SID("depth"), SID("depthPrepass")));
    const VkImageMemoryBarrier2* depthPreClear = inspector.FindPreClearBarrier(0, SID("depth"));
    REQUIRE(depthPreClear != nullptr);
    CHECK(depthPreClear->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // gbuffer color-attachment transition in its producing wave.
    const VkImageMemoryBarrier2* gbufBarrier = inspector.FindImageBarrier(1, SID("gbuffer"));
    REQUIRE(gbufBarrier != nullptr);
    CHECK(gbufBarrier->newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // drawBuf is read in wave 1 after being written in wave 0: RAW buffer barrier present.
    const VkBufferMemoryBarrier2* drawBarrier = inspector.FindBufferBarrier(1, SID("drawBuf"));
    REQUIRE(drawBarrier != nullptr);
    CHECK((drawBarrier->dstAccessMask & VK_ACCESS_2_SHADER_READ_BIT) != 0);
}

// ================================================================================
// Section 2: Scheduling & dependency edges
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: single pass is wave 0 with no edges", "[rdg][schedule]")
{
    rdg.AddPass(SID("only"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    Compile();
    CHECK(inspector.SortedPassCount() == 1);
    CHECK(inspector.WaveCount() == 1);
    CHECK(inspector.PassWaveIndex(SID("only")) == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: independent passes share wave 0 and have no edges", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("a"), TexInfo());
    rdg.CreateTexture(SID("b"), TexInfo(640, 480));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("a"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("b"));
    Compile();
    CHECK(inspector.WaveCount() == 1);
    CHECK_FALSE(inspector.HasEdge(SID("pA"), SID("pB")));
    CHECK_FALSE(inspector.HasEdge(SID("pB"), SID("pA")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: linear chain produces sequential waves and edges", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("t0"), TexInfo());
    rdg.CreateTexture(SID("t1"), TexInfo(640, 480));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t0"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("t0")).WriteStorageImage(SID("t1"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("t1"));
    Compile();
    CHECK(inspector.WaveCount() == 3);
    CHECK(inspector.PassWaveIndex(SID("pA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("pB")) == 1);
    CHECK(inspector.PassWaveIndex(SID("pC")) == 2);
    CHECK(inspector.HasEdge(SID("pA"), SID("pB")));
    CHECK(inspector.HasEdge(SID("pB"), SID("pC")));
    CHECK_FALSE(inspector.HasEdge(SID("pA"), SID("pC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: diamond has exactly the four expected edges, no cross edge", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("in"), TexInfo());
    rdg.CreateTexture(SID("b"), TexInfo(640, 480));
    rdg.CreateTexture(SID("c"), TexInfo(320, 240));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("in"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("in")).WriteStorageImage(SID("b"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("in")).WriteStorageImage(SID("c"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("b")).ReadStorageImage(SID("c"));
    Compile();
    CHECK(inspector.HasEdge(SID("pA"), SID("pB")));
    CHECK(inspector.HasEdge(SID("pA"), SID("pC")));
    CHECK(inspector.HasEdge(SID("pB"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pC"), SID("pD")));
    CHECK_FALSE(inspector.HasEdge(SID("pB"), SID("pC")));
    CHECK_FALSE(inspector.HasEdge(SID("pC"), SID("pB")));
    CHECK(inspector.PassWaveIndex(SID("pB")) == inspector.PassWaveIndex(SID("pC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: write-read-write produces RAW, WAR and WAW edges", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("p2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile();
    CHECK(inspector.HasEdge(SID("p0"), SID("p1"))); // RAW
    CHECK(inspector.HasEdge(SID("p1"), SID("p2"))); // WAR
    CHECK(inspector.HasEdge(SID("p0"), SID("p2"))); // WAW
}

TEST_CASE_METHOD(RdgFixture, "RDG: same-layout readers depend on the transition cause, not each other", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    Compile();
    CHECK(inspector.HasEdge(SID("pA"), SID("pB")));
    CHECK(inspector.HasEdge(SID("pA"), SID("pC")));
    CHECK_FALSE(inspector.HasEdge(SID("pB"), SID("pC")));
    CHECK(inspector.PassWaveIndex(SID("pB")) == inspector.PassWaveIndex(SID("pC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: readers in different layouts serialize on layout change", "[rdg][schedule]")
{
    // Storage-read (GENERAL) then sampled-read (SHADER_READ_ONLY) of the same texture: the second
    // reader must wait on the first because the layout epoch changes.
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pStorageRead"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pSampledRead"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    Compile();
    CHECK(inspector.HasEdge(SID("pStorageRead"), SID("pSampledRead")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer RAW, WAW and WAR edges", "[rdg][schedule]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("buf"));
    rdg.AddPass(SID("p2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    Compile();
    CHECK(inspector.HasEdge(SID("p0"), SID("p1")));
    CHECK(inspector.HasEdge(SID("p1"), SID("p2")));
    CHECK(inspector.HasEdge(SID("p0"), SID("p2")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: cross-epoch RAWs on same texture both produce correct edges", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.CreateTexture(SID("other"), TexInfo(640, 480));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("other"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex")).ReadStorageImage(SID("other"));
    rdg.AddPass(SID("pE"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile();
    CHECK(inspector.HasEdge(SID("pA"), SID("pB")));
    CHECK(inspector.HasEdge(SID("pB"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pC"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pD"), SID("pE")));
    CHECK(inspector.HasEdge(SID("pA"), SID("pE")));
    CHECK(inspector.PassWaveIndex(SID("pD")) > inspector.PassWaveIndex(SID("pB")));
    CHECK(inspector.PassWaveIndex(SID("pD")) > inspector.PassWaveIndex(SID("pC")));
    CHECK(inspector.PassWaveIndex(SID("pE")) > inspector.PassWaveIndex(SID("pD")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: parallel texture readers both produce WAR edges to next write", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadSampledImage(SID("tex"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile();
    CHECK(inspector.HasEdge(SID("pA"), SID("pB")));
    CHECK(inspector.HasEdge(SID("pA"), SID("pC")));
    CHECK(inspector.HasEdge(SID("pB"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pC"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pA"), SID("pD")));
    CHECK_FALSE(inspector.HasEdge(SID("pB"), SID("pC")));
    CHECK(inspector.PassWaveIndex(SID("pB")) == inspector.PassWaveIndex(SID("pC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: parallel readers both edge to reader in new layout", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    Compile();
    CHECK(inspector.HasEdge(SID("pB"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pC"), SID("pD")));
    CHECK_FALSE(inspector.HasEdge(SID("pB"), SID("pC")));
    CHECK(inspector.PassWaveIndex(SID("pB")) == inspector.PassWaveIndex(SID("pC")));
    CHECK(inspector.PassWaveIndex(SID("pD")) > inspector.PassWaveIndex(SID("pB")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: parallel buffer readers both produce WAR edges to next write", "[rdg][schedule]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("buf"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("buf"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    Compile();
    CHECK(inspector.HasEdge(SID("pA"), SID("pB")));
    CHECK(inspector.HasEdge(SID("pA"), SID("pC")));
    CHECK(inspector.HasEdge(SID("pB"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pC"), SID("pD")));
    CHECK(inspector.HasEdge(SID("pA"), SID("pD")));
    CHECK_FALSE(inspector.HasEdge(SID("pB"), SID("pC")));
    CHECK(inspector.PassWaveIndex(SID("pB")) == inspector.PassWaveIndex(SID("pC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: two producers feeding a merger put the merger one wave later", "[rdg][schedule]")
{
    rdg.CreateTexture(SID("tA"), TexInfo());
    rdg.CreateTexture(SID("tB"), TexInfo(640, 480));
    rdg.AddPass(SID("prodA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tA"));
    rdg.AddPass(SID("prodB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tB"));
    rdg.AddPass(SID("merge"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tA")).ReadStorageImage(SID("tB"));
    Compile();
    CHECK(inspector.WaveCount() == 2);
    CHECK(inspector.PassWaveIndex(SID("prodA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("prodB")) == 0);
    CHECK(inspector.PassWaveIndex(SID("merge")) == 1);
}

// ================================================================================
// Section 3: Usage accumulation (exact masks)
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: storage write/read sets STORAGE_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("r"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: sampled read sets SAMPLED_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: color attachment sets COLOR_ATTACHMENT_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("color"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT).WriteColorAttachment(SID("color"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("color")) & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: depth attachment sets DEPTH_STENCIL and SAMPLED", "[rdg][usage]")
{
    rdg.CreateTexture(SID("depth"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT).WriteDepthAttachment(SID("depth"));
    rdg.AccumulateUsage();
    const VkImageUsageFlags usage = inspector.TextureAccumulatedUsage(SID("depth"));
    CHECK((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
    CHECK((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: blit and copy read/write set TRANSFER_SRC/DST", "[rdg][usage]")
{
    rdg.CreateTexture(SID("bsrc"), TexInfo());
    rdg.CreateTexture(SID("bdst"), TexInfo(640, 480));
    rdg.CreateTexture(SID("csrc"), TexInfo(320, 240));
    rdg.CreateTexture(SID("cdst"), TexInfo(160, 120));
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_BLIT_BIT)
            .ReadBlitImage(SID("bsrc")).WriteBlitImage(SID("bdst")).ReadCopyImage(SID("csrc")).WriteCopyImage(SID("cdst"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("bsrc")) & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(SID("bdst")) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(SID("csrc")) & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0);
    CHECK((inspector.TextureAccumulatedUsage(SID("cdst")) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: clear-value texture gets TRANSFER_DST automatically", "[rdg][usage]")
{
    VkClearValue cv{};
    cv.color = {{0.f, 0.f, 0.f, 1.f}};
    rdg.CreateTexture(SID("tex"), TexInfo(), cv);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer write sets STORAGE and DEVICE_ADDRESS", "[rdg][usage]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AccumulateUsage();
    const VkBufferUsageFlags usage = inspector.BufferAccumulatedUsage(SID("buf"));
    CHECK((usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0);
    CHECK((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: index, indirect and indirect-count buffer usages", "[rdg][usage]")
{
    rdg.CreateBuffer(SID("idx"), 1024);
    rdg.CreateBuffer(SID("indirect"), 1024);
    rdg.CreateBuffer(SID("count"), 1024);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT)
            .ReadIndexBuffer(SID("idx")).ReadIndirectBuffer(SID("indirect")).ReadIndirectCountBuffer(SID("count"));
    rdg.AccumulateUsage();
    CHECK((inspector.BufferAccumulatedUsage(SID("idx")) & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(SID("indirect")) & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(SID("count")) & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: transfer buffer read/write set TRANSFER_SRC/DST", "[rdg][usage]")
{
    rdg.CreateBuffer(SID("src"), 1024);
    rdg.CreateBuffer(SID("dst"), 1024);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COPY_BIT).ReadTransferBuffer(SID("src")).WriteTransferBuffer(SID("dst"));
    rdg.AccumulateUsage();
    CHECK((inspector.BufferAccumulatedUsage(SID("src")) & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0);
    CHECK((inspector.BufferAccumulatedUsage(SID("dst")) & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: multiple access types union into accumulated usage", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("r"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    rdg.AccumulateUsage();
    const VkImageUsageFlags usage = inspector.TextureAccumulatedUsage(SID("tex"));
    CHECK((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
    CHECK((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: writes through an alias accumulate on the base logical resource", "[rdg][usage]")
{
    rdg.CreateTexture(SID("base"), TexInfo());
    rdg.AliasTexture(SID("alias"), SID("base"));
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("alias"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("base")) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
}

// ================================================================================
// Section 4: Lifetimes
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: lifetime spans first and last sorted pass that use a texture", "[rdg][lifetime]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    Compile();
    CHECK(inspector.TextureFirstPass(SID("tex")) == 0);
    CHECK(inspector.TextureLastPass(SID("tex")) == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: middle-only resource has a tight lifetime", "[rdg][lifetime]")
{
    rdg.CreateTexture(SID("t0"), TexInfo());
    rdg.CreateTexture(SID("t1"), TexInfo(640, 480));
    rdg.CreateTexture(SID("t2"), TexInfo(320, 240));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t0"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("t0")).WriteStorageImage(SID("t1"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("t1")).WriteStorageImage(SID("t2"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("t2"));
    Compile();
    CHECK(inspector.TextureFirstPass(SID("t1")) == 1);
    CHECK(inspector.TextureLastPass(SID("t1")) == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer lifetime tracked across passes", "[rdg][lifetime]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("buf"));
    Compile();
    CHECK(inspector.BufferFirstPass(SID("buf")) == 0);
    CHECK(inspector.BufferLastPass(SID("buf")) == 1);
}

// ================================================================================
// Section 5: Auto-clear (into the barrier stream)
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: clear-value texture is cleared only in its first pass", "[rdg][autoclear]")
{
    VkClearValue cv{};
    cv.color = {{0.f, 0.f, 0.f, 1.f}};
    rdg.CreateTexture(SID("clearTex"), TexInfo(), cv);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("clearTex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("clearTex"));
    Compile();
    CHECK(inspector.TextureHasClearInPass(SID("clearTex"), SID("pA")));
    CHECK_FALSE(inspector.TextureHasClearInPass(SID("clearTex"), SID("pB")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: texture without a clear value is never auto-cleared", "[rdg][autoclear]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile();
    CHECK_FALSE(inspector.TextureHasClearInPass(SID("tex"), SID("pA")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: clear-value texture first used in a later wave emits a pre-clear barrier", "[rdg][autoclear][barrier]")
{
    // producer occupies wave 0; the clear texture is first touched in wave 1.
    VkClearValue cv{};
    cv.color = {{1.f, 0.f, 0.f, 1.f}};
    rdg.CreateTexture(SID("seed"), TexInfo());
    rdg.CreateTexture(SID("clearTex"), TexInfo(640, 480), cv);
    rdg.AddPass(SID("produce"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("seed"));
    rdg.AddPass(SID("consume"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .ReadStorageImage(SID("seed")).WriteStorageImage(SID("clearTex"));
    Compile();

    CHECK(inspector.PassWaveIndex(SID("consume")) == 1);
    CHECK(inspector.WavePreClearCount(1) >= 1);
    const VkImageMemoryBarrier2* preClear = inspector.FindPreClearBarrier(1, SID("clearTex"));
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
    rdg.CreateTexture(SID("texA"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo(640, 480));
    rdg.CreateTexture(SID("texC"), TexInfo()); // same dims as texA
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("texA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texA")).WriteStorageImage(SID("texB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texB")).WriteStorageImage(SID("texC"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texC"));
    Compile();
    CHECK(inspector.TexturesSharePhysical(SID("texA"), SID("texC")));
    CHECK(inspector.TextureImage(SID("texA")) == inspector.TextureImage(SID("texC")));
    CHECK_FALSE(inspector.TexturesSharePhysical(SID("texA"), SID("texB")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: overlapping same-dim textures do not alias", "[rdg][aliasing]")
{
    rdg.CreateTexture(SID("texA"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("texA")).WriteStorageImage(SID("texB"));
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(SID("texA"), SID("texB")));
    CHECK(inspector.PhysicalCount() >= 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: different-dimension textures never alias", "[rdg][aliasing]")
{
    rdg.CreateTexture(SID("big"), TexInfo(1920, 1080));
    rdg.CreateTexture(SID("small"), TexInfo(960, 540));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("big"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("small"));
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(SID("big"), SID("small")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: imported textures are never aliased with transients", "[rdg][aliasing]")
{
    auto img = reinterpret_cast<VkImage>(0x1111ull);
    auto view = reinterpret_cast<VkImageView>(0x2222ull);
    rdg.ImportTexture(SID("imported"), img, view, TexInfo(), VK_IMAGE_USAGE_STORAGE_BIT,
                      VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rdg.CreateTexture(SID("transient"), TexInfo());
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("imported"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("transient"));
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(SID("imported"), SID("transient")));
    CHECK(inspector.PhysicalIsImported(inspector.TexturePhysicalIndex(SID("imported"))));
}

TEST_CASE_METHOD(RdgFixture, "RDG: aliased physical grows usage to the union of its tenants", "[rdg][aliasing]")
{
    // texA is storage-only; texC (same dims, non-overlapping life) is sampled+storage. The shared
    // physical must carry both usage bits.
    rdg.CreateTexture(SID("texA"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo(640, 480));
    rdg.CreateTexture(SID("texC"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("texA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texA")).WriteStorageImage(SID("texB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texB")).WriteStorageImage(SID("texC"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("texC"));
    Compile();
    REQUIRE(inspector.TexturesSharePhysical(SID("texA"), SID("texC")));
    const VkImageUsageFlags physUsage = inspector.PhysicalImageUsage(inspector.TexturePhysicalIndex(SID("texA")));
    CHECK((physUsage & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
    CHECK((physUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: viewport-scaled flag propagates onto the physical", "[rdg][aliasing]")
{
    rdg.CreateTexture(SID("vp"), TexInfo(), std::nullopt, /*bIsViewportScaled=*/true);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("vp"));
    Compile();
    CHECK(inspector.PhysicalIsViewportScaled(inspector.TexturePhysicalIndex(SID("vp"))));
}

TEST_CASE_METHOD(RdgFixture, "RDG: a carried-over texture cannot alias even with a free slot", "[rdg][aliasing]")
{
    // texA is marked for carryover (bCanUseAliasedTexture=false); texC has a non-overlapping life and
    // identical dims but still must not reuse texA's physical.
    rdg.CreateTexture(SID("texA"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo(640, 480));
    rdg.CreateTexture(SID("texC"), TexInfo());
    rdg.CarryTextureToNextFrame(SID("texA"), SID("texA_next"), VK_IMAGE_USAGE_STORAGE_BIT);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("texA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texA")).WriteStorageImage(SID("texB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texB")).WriteStorageImage(SID("texC"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texC"));
    Compile();
    CHECK_FALSE(inspector.TexturesSharePhysical(SID("texA"), SID("texC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: chain reuses head/tail into one physical, distinct middle", "[rdg][aliasing]")
{
    rdg.CreateTexture(SID("A"), TexInfo());
    rdg.CreateTexture(SID("B"), TexInfo(640, 480));
    rdg.CreateTexture(SID("C"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("A"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("A")).WriteStorageImage(SID("B"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("B")).WriteStorageImage(SID("C"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("C"));
    Compile();
    CHECK(inspector.TexturesSharePhysical(SID("A"), SID("C")));
    CHECK(inspector.PhysicalCount() == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: overlapping same-size buffers do not alias", "[rdg][aliasing]")
{
    rdg.CreateBuffer(SID("bufA"), 4096);
    rdg.CreateBuffer(SID("bufB"), 4096);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("bufA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("bufA")).WriteBuffer(SID("bufB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("bufB"));
    Compile();
    CHECK_FALSE(inspector.BuffersSharePhysical(SID("bufA"), SID("bufB")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: non-overlapping same-size buffers alias", "[rdg][aliasing]")
{
    // bufA lives in waves 0-1; bufC lives in waves 2-3 (same size) -> alias.
    rdg.CreateBuffer(SID("bufA"), 4096);
    rdg.CreateBuffer(SID("bufB"), 2048);
    rdg.CreateBuffer(SID("bufC"), 4096);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("bufA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("bufA")).WriteBuffer(SID("bufB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("bufB")).WriteBuffer(SID("bufC"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("bufC"));
    Compile();
    CHECK(inspector.BuffersSharePhysical(SID("bufA"), SID("bufC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: bCanAlias=false buffer is always its own physical", "[rdg][aliasing]")
{
    rdg.CreateBuffer(SID("noAlias"), 4096, false, /*bCanAlias=*/false);
    rdg.CreateBuffer(SID("normal"), 4096);
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("noAlias"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("normal"));
    Compile();
    CHECK_FALSE(inspector.BuffersSharePhysical(SID("noAlias"), SID("normal")));
    CHECK_FALSE(inspector.PhysicalCanAlias(inspector.BufferPhysicalIndex(SID("noAlias"))));
}

// ================================================================================
// Section 7: Barriers (PrecomputeBarriers output)
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: first storage write barrier transitions UNDEFINED to GENERAL", "[rdg][barrier]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, SID("tex"));
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
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("r"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(1, SID("tex"));
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
    rdg.CreateTexture(SID("color"), TexInfo());
    rdg.AddPass(SID("draw"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT).WriteColorAttachment(SID("color"));
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, SID("color"));
    REQUIRE(b != nullptr);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(b->dstAccessMask == (VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT));
}

TEST_CASE_METHOD(RdgFixture, "RDG: depth write barrier uses fragment-test stages and write access", "[rdg][barrier]")
{
    rdg.CreateTexture(SID("depth"), DepthTexInfo());
    rdg.AddPass(SID("z"), VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT).WriteDepthAttachment(SID("depth"));
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, SID("depth"));
    REQUIRE(b != nullptr);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK(b->dstStageMask == (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT));
    CHECK(b->dstAccessMask == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    CHECK((inspector.PhysicalAspect(inspector.TexturePhysicalIndex(SID("depth"))) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: read-only depth barrier carries read access only", "[rdg][barrier]")
{
    rdg.CreateTexture(SID("depth"), DepthTexInfo());
    rdg.AddPass(SID("z"), VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT).WriteDepthAttachment(SID("depth"));
    rdg.AddPass(SID("read"), VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT).ReadDepthAttachment(SID("depth"));
    Compile();
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(1, SID("depth"));
    REQUIRE(b != nullptr);
    CHECK(b->dstAccessMask == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: blit read/write barriers use BLIT stage and TRANSFER layouts", "[rdg][barrier]")
{
    rdg.CreateTexture(SID("src"), TexInfo());
    rdg.CreateTexture(SID("dst"), TexInfo(640, 480));
    rdg.AddPass(SID("seed"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("src"));
    rdg.AddPass(SID("blit"), VK_PIPELINE_STAGE_2_BLIT_BIT).ReadBlitImage(SID("src")).WriteBlitImage(SID("dst"));
    Compile();
    const VkImageMemoryBarrier2* read = inspector.FindImageBarrier(1, SID("src"));
    const VkImageMemoryBarrier2* write = inspector.FindImageBarrier(1, SID("dst"));
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
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AddPass(SID("r"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("buf"));
    Compile();
    const VkBufferMemoryBarrier2* b = inspector.FindBufferBarrier(1, SID("buf"));
    REQUIRE(b != nullptr);
    CHECK(b->srcStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(b->srcAccessMask == VK_ACCESS_2_SHADER_WRITE_BIT);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(b->dstAccessMask == VK_ACCESS_2_SHADER_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: index buffer barrier uses INDEX_INPUT stage and INDEX_READ access", "[rdg][barrier]")
{
    rdg.CreateBuffer(SID("idx"), 1024);
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("idx"));
    rdg.AddPass(SID("draw"), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT).ReadIndexBuffer(SID("idx"));
    Compile();
    const VkBufferMemoryBarrier2* b = inspector.FindBufferBarrier(1, SID("idx"));
    REQUIRE(b != nullptr);
    CHECK(b->dstStageMask == VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
    CHECK(b->dstAccessMask == VK_ACCESS_2_INDEX_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: indirect buffer barrier carries draw-indirect and shader-read", "[rdg][barrier]")
{
    rdg.CreateBuffer(SID("indirect"), 1024);
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("indirect"));
    rdg.AddPass(SID("draw"), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT).ReadIndirectBuffer(SID("indirect"));
    Compile();
    const VkBufferMemoryBarrier2* b = inspector.FindBufferBarrier(1, SID("indirect"));
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
    rdg.ImportTexture(SID("shared"), img, view, TexInfo(), VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rdg.AddPass(SID("readA"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("shared"));
    rdg.AddPass(SID("readB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadSampledImage(SID("shared"));
    Compile();
    CHECK(inspector.PassWaveIndex(SID("readA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("readB")) == 0);
    const VkImageMemoryBarrier2* b = inspector.FindImageBarrier(0, SID("shared"));
    REQUIRE(b != nullptr);
    CHECK(b->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(b->dstStageMask == (VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    CHECK(b->dstAccessMask == VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

TEST_CASE_METHOD(RdgFixture, "RDG: no-barrier imported buffer emits no buffer barriers", "[rdg][barrier]")
{
    auto buf = reinterpret_cast<VkBuffer>(0xB0FFEEull);
    rdg.ImportBufferNoBarrier(SID("ext"), buf, 0x1000, {1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    rdg.AddPass(SID("w"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("ext"));
    rdg.AddPass(SID("r"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("ext"));
    Compile();
    CHECK(inspector.PhysicalDisableBarriers(inspector.BufferPhysicalIndex(SID("ext"))));
    CHECK(inspector.TotalBufferBarriers() == 0);
}

// ================================================================================
// Section 8: Imported resources
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: ImportTexture records initial state, aspect and final layout", "[rdg][import]")
{
    auto img = reinterpret_cast<VkImage>(0xD00D01ull);
    auto view = reinterpret_cast<VkImageView>(0xD00D02ull);
    rdg.ImportTexture(SID("imp"), img, view, DepthTexInfo(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const uint32_t physIdx = inspector.TexturePhysicalIndex(SID("imp"));
    CHECK(inspector.PhysicalIsImported(physIdx));
    CHECK(inspector.PhysicalImage(physIdx) == img);
    CHECK(inspector.TextureLayout(SID("imp")) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CHECK(inspector.TextureFinalLayout(SID("imp")) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK((inspector.PhysicalAspect(physIdx) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: re-importing the same image next frame reuses its physical", "[rdg][import]")
{
    auto img = reinterpret_cast<VkImage>(0xE0E0E0ull);
    auto view = reinterpret_cast<VkImageView>(0xE0E0E1ull);
    rdg.ImportTexture(SID("imp"), img, view, TexInfo(), VK_IMAGE_USAGE_STORAGE_BIT,
                      VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("imp"));
    Compile(0);
    const size_t physCount = inspector.PhysicalCount();

    NextFrame(0, 1);
    rdg.ImportTexture(SID("imp"), img, view, TexInfo(), VK_IMAGE_USAGE_STORAGE_BIT,
                      VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("imp"));
    Compile(1);
    CHECK(inspector.PhysicalCount() == physCount);
    CHECK(inspector.PhysicalImage(inspector.TexturePhysicalIndex(SID("imp"))) == img);
}

TEST_CASE_METHOD(RdgFixture, "RDG: ImportBuffer with device-address usage retrieves a deterministic address", "[rdg][import]")
{
    auto buf = reinterpret_cast<VkBuffer>(0xF00001ull);
    PipelineEvent state{VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT};
    rdg.ImportBuffer(SID("ext"), buf, 0, {2048, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, state);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("ext"));
    Compile();

    const uint32_t physIdx = inspector.BufferPhysicalIndex(SID("ext"));
    CHECK(inspector.PhysicalAddressRetrieved(physIdx));
    auto val = reinterpret_cast<uint64_t>(buf);
    const VkDeviceAddress expected = fnv1a64(reinterpret_cast<const uint8_t*>(&val), sizeof(val));
    CHECK(rdg.GetBufferAddress(SID("ext")) == expected);
}

// ================================================================================
// Section 9: Multi-frame reset, eviction & carryover
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: Reset removes previously registered logical resources", "[rdg][reset]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.CreateBuffer(SID("buf"), 1024);
    CHECK(rdg.HasTexture(SID("tex")));
    CHECK(rdg.HasBuffer(SID("buf")));
    NextFrame();
    CHECK_FALSE(rdg.HasTexture(SID("tex")));
    CHECK_FALSE(rdg.HasBuffer(SID("buf")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: identical consecutive frames reuse physicals without growth", "[rdg][reset]")
{
    auto build = [&]() {
        rdg.CreateTexture(SID("t"), TexInfo());
        rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t"));
        rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("t"));
    };
    build();
    Compile(0);
    const size_t phys0 = inspector.PhysicalCount();

    NextFrame(0, 1);
    build();
    Compile(1);
    CHECK(inspector.PhysicalCount() == phys0);
    CHECK(inspector.PassWaveIndex(SID("p0")) == 0);
    CHECK(inspector.PassWaveIndex(SID("p1")) == 1);
}

TEST_CASE_METHOD(RdgFixture, "RDG: a physical unused beyond maxFramesUnused is evicted", "[rdg][reset]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile(0);
    CHECK(inspector.PhysicalCount() >= 1);

    rdg.Reset(0, 1, 1); // frame 1, maxUnused=1: nothing uses the texture this frame
    Compile(1);
    rdg.Reset(0, 2, 1); // frame 2: unused for one full frame -> evicted
    CHECK(inspector.PhysicalCount() == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: texture carryover preserves the physical under the new name", "[rdg][reset][carryover]")
{
    rdg.CreateTexture(SID("history"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("history"));
    rdg.CarryTextureToNextFrame(SID("history"), SID("historyPrev"), VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    CHECK(inspector.TextureCarryoverCount() == 1);
    Compile(0);
    VkImage carriedImage = inspector.TextureImage(SID("history"));

    NextFrame(0, 1);
    CHECK(rdg.HasTexture(SID("historyPrev")));
    const uint32_t physIdx = inspector.TexturePhysicalIndex(SID("historyPrev"));
    CHECK(inspector.PhysicalImage(physIdx) == carriedImage);
    CHECK_FALSE(inspector.PhysicalCanAlias(physIdx));
    CHECK(inspector.TextureCarryoverCount() == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: buffer carryover preserves the physical under the new name", "[rdg][reset][carryover]")
{
    rdg.CreateBuffer(SID("accum"), 4096);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("accum"));
    rdg.CarryBufferToNextFrame(SID("accum"), SID("accumPrev"), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Compile(0);
    VkBuffer carriedBuffer = inspector.BufferHandle(SID("accum"));

    NextFrame(0, 1);
    CHECK(rdg.HasBuffer(SID("accumPrev")));
    CHECK(inspector.BufferHandle(SID("accumPrev")) == carriedBuffer);
}

TEST_CASE_METHOD(RdgFixture, "RDG: viewport-scaled physical is evicted on InvalidateAllViewportAssociated", "[rdg][reset][viewport]")
{
    rdg.CreateTexture(SID("vp"), TexInfo(), std::nullopt, true);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("vp"));
    Compile(0);
    CHECK(inspector.PhysicalCount() == 1);
    rdg.InvalidateAllViewportAssociated();
    rdg.Reset(0, 1, 100);
    CHECK(inspector.PhysicalCount() == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: non-viewport physical survives InvalidateAllViewportAssociated", "[rdg][reset][viewport]")
{
    rdg.CreateTexture(SID("vp"), TexInfo(), std::nullopt, true);
    rdg.CreateTexture(SID("static"), TexInfo(64, 64));
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("vp"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("static"));
    Compile(0);
    rdg.InvalidateAllViewportAssociated();
    rdg.Reset(0, 1, 100);
    CHECK(inspector.PhysicalCount() == 1);
    CHECK_FALSE(inspector.PhysicalIsViewportScaled(0));
}

TEST_CASE_METHOD(RdgFixture, "RDG: swapchain physical is removed on InvalidateAllSwapchainAssociated", "[rdg][reset][swapchain]")
{
    auto img = reinterpret_cast<VkImage>(0x5C0001ull);
    auto view = reinterpret_cast<VkImageView>(0x5C0002ull);
    rdg.ImportTexture(SID("swap"), img, view, TexInfo(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT).WriteColorAttachment(SID("swap"));
    Compile(0);
    CHECK(inspector.PhysicalCount() == 1);
    rdg.InvalidateAllSwapchainAssociated();
    rdg.Reset(0, 1, 100);
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
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("write"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));

    bool ran = false;
    uint32_t resolvedIndex = Core::INVALID_HANDLE_INDEX;
    rdg.AddPass(SID("read"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT)
            .ReadSampledImage(SID("tex"))
            .Execute([&](VkCommandBuffer) {
                ran = true;
                resolvedIndex = rdg.GetSampledImageViewDescriptorIndex(SID("tex"));
            });

    Compile();
    rdg.Execute(reinterpret_cast<VkCommandBuffer>(uintptr_t{1}));

    CHECK(ran);
    const uint32_t physIdx = inspector.TexturePhysicalIndex(SID("tex"));
    CHECK(inspector.PhysicalSampledDescriptorValid(physIdx));
    CHECK(resolvedIndex != Core::INVALID_HANDLE_INDEX);
    CHECK(resolvedIndex == inspector.PhysicalSampledDescriptorIndex(physIdx));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Execute resolves buffer device address inside the lambda", "[rdg][execute]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("write"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));

    bool ran = false;
    VkDeviceAddress resolvedAddress = 0;
    rdg.AddPass(SID("read"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .ReadBuffer(SID("buf"))
            .Execute([&](VkCommandBuffer) {
                ran = true;
                resolvedAddress = rdg.GetBufferAddress(SID("buf"));
            });

    Compile();
    rdg.Execute(reinterpret_cast<VkCommandBuffer>(uintptr_t{1}));

    CHECK(ran);
    CHECK(resolvedAddress != 0);
    const uint32_t physIdx = inspector.BufferPhysicalIndex(SID("buf"));
    CHECK(inspector.PhysicalAddressRetrieved(physIdx));
    CHECK(resolvedAddress == inspector.PhysicalBufferAddress(physIdx));
}
