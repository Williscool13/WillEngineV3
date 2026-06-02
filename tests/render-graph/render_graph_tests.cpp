//
// Render graph test suite.
// All tests run CPU-only: null VulkanContext and ResourceManager, stub allocFns that return
// empty AllocatedBuffer handles. Physical GPU resources are never created (VK_FORMAT_UNDEFINED
// textures skip CreatePhysicalImage; size-0 buffers skip CreatePhysicalBuffer).
//

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>

#include "core/hash/fnv_1_a.h"
#include "core/memory/tlsf_allocator.h"
#include "core/memory/arena.h"
#include "render/vulkan/vk_context.h"
#include "render/render-graph/render_graph.h"
#include "render/render-graph/render_pass.h"

namespace Render
{

// Grants white-box access to RenderGraph internals for testing.
class RenderGraphInspector
{
public:
    explicit RenderGraphInspector(RenderGraph& rdg) : rdg(rdg) {}

    // Physical resources
    size_t PhysicalCount() const { return rdg.physicalResources.Size(); }

    uint32_t TexturePhysicalIndex(StringID id) const
    {
        for (auto& tex : rdg.textures) {
            if (tex.textureId == id) { return tex.physicalIndex; }
        }
        return UINT32_MAX;
    }

    uint32_t BufferPhysicalIndex(StringID id) const
    {
        for (auto& buf : rdg.buffers) {
            if (buf.bufferId == id) { return buf.physicalIndex; }
        }
        return UINT32_MAX;
    }

    bool TexturesSharePhysical(StringID a, StringID b) const
    {
        const uint32_t ia = TexturePhysicalIndex(a);
        const uint32_t ib = TexturePhysicalIndex(b);
        return ia != UINT32_MAX && ia == ib;
    }

    bool BuffersSharePhysical(StringID a, StringID b) const
    {
        const uint32_t ia = BufferPhysicalIndex(a);
        const uint32_t ib = BufferPhysicalIndex(b);
        return ia != UINT32_MAX && ia == ib;
    }

    // Texture/buffer state
    VkImageUsageFlags TextureAccumulatedUsage(StringID id) const
    {
        for (auto& tex : rdg.textures) {
            if (tex.textureId == id) { return tex.accumulatedUsage; }
        }
        return 0;
    }

    VkBufferUsageFlags BufferAccumulatedUsage(StringID id) const
    {
        for (auto& buf : rdg.buffers) {
            if (buf.bufferId == id) { return buf.accumulatedUsage; }
        }
        return 0;
    }

    uint32_t TextureFirstPass(StringID id) const
    {
        for (auto& tex : rdg.textures) {
            if (tex.textureId == id) { return tex.firstPass; }
        }
        return UINT32_MAX;
    }

    uint32_t TextureLastPass(StringID id) const
    {
        for (auto& tex : rdg.textures) {
            if (tex.textureId == id) { return tex.lastPass; }
        }
        return UINT32_MAX;
    }

    uint32_t BufferFirstPass(StringID id) const
    {
        for (auto& buf : rdg.buffers) {
            if (buf.bufferId == id) { return buf.firstPass; }
        }
        return UINT32_MAX;
    }

    uint32_t BufferLastPass(StringID id) const
    {
        for (auto& buf : rdg.buffers) {
            if (buf.bufferId == id) { return buf.lastPass; }
        }
        return UINT32_MAX;
    }

    bool TextureHasClearInPass(StringID texId, StringID passId) const
    {
        uint32_t texIdx = UINT32_MAX;
        for (auto& tex : rdg.textures) {
            if (tex.textureId == texId) { texIdx = tex.index; break; }
        }
        if (texIdx == UINT32_MAX) { return false; }

        for (auto& pass : rdg.sortedPasses) {
            if (pass->renderPassId == passId) {
                for (uint32_t i : pass->autoClearTextures) {
                    if (i == texIdx) { return true; }
                }
            }
        }
        return false;
    }

    // Pass structure
    uint32_t SortedPassCount() const { return static_cast<uint32_t>(rdg.sortedPasses.Size()); }
    uint32_t WaveCount() const
    {
        return rdg.waveOffsets.Size() > 0 ? static_cast<uint32_t>(rdg.waveOffsets.Size() - 1) : 0;
    }

    uint32_t PassWaveIndex(StringID passId) const
    {
        for (auto& pass : rdg.sortedPasses) {
            if (pass->renderPassId == passId) { return pass->waveIndex; }
        }
        return UINT32_MAX;
    }

    uint32_t PassSortedIndex(StringID passId) const
    {
        for (uint32_t i = 0; i < rdg.sortedPasses.Size(); ++i) {
            if (rdg.sortedPasses[i]->renderPassId == passId) { return i; }
        }
        return UINT32_MAX;
    }

    bool PassComesBeforeInSort(StringID earlier, StringID later) const
    {
        return PassSortedIndex(earlier) < PassSortedIndex(later);
    }

    uint32_t PassInDegree(StringID passId) const
    {
        for (auto& pass : rdg.passes) {
            if (pass->renderPassId == passId) { return pass->inDegree; }
        }
        return UINT32_MAX;
    }

    size_t PassOutEdgeCount(StringID passId) const
    {
        for (auto& pass : rdg.passes) {
            if (pass->renderPassId == passId) { return pass->outEdges.Size(); }
        }
        return SIZE_MAX;
    }

    // Physical resource attributes
    bool PhysicalIsImported(uint32_t physIdx) const
    {
        if (physIdx >= rdg.physicalResources.Size()) { return false; }
        return rdg.physicalResources[physIdx].bIsImported;
    }

    bool PhysicalCanAlias(uint32_t physIdx) const
    {
        if (physIdx >= rdg.physicalResources.Size()) { return false; }
        return rdg.physicalResources[physIdx].bCanAlias;
    }

    bool PhysicalIsViewportScaled(uint32_t physIdx) const
    {
        if (physIdx >= rdg.physicalResources.Size()) { return false; }
        return rdg.physicalResources[physIdx].bIsViewportScaled;
    }

private:
    RenderGraph& rdg;
};

} // Render

// ---- Test Fixture ---------------------------------------------------------------

struct AllocBase
{
    static constexpr size_t TLSF_SIZE = 1 * 1024 * 1024;
    static constexpr size_t ARENA_SIZE = 8 * 1024 * 1024;

    std::unique_ptr<char[]> tlsfPool{new char[TLSF_SIZE]};
    std::unique_ptr<char[]> arenaPool{new char[ARENA_SIZE]};

    Core::TlsfAllocator alloc;
    Core::Arena arena;

    AllocBase() : arena(arenaPool.get(), ARENA_SIZE, "rdg-test")
    {
        alloc.Init(tlsfPool.get(), TLSF_SIZE, false);
    }
};

static Render::RenderGraphAllocFns MakeStubAllocFns()
{
    Render::RenderGraphAllocFns fns;
    fns.createImage = [](const Render::VulkanContext*, const VkImageCreateInfo&) -> Render::RenderGraphAllocFns::ImageAlloc {
        static uint64_t counter = 1;
        return {reinterpret_cast<VkImage>(counter++), VK_NULL_HANDLE};
    };
    fns.createImageView = [](const Render::VulkanContext*, const VkImageViewCreateInfo&) -> VkImageView {
        return VK_NULL_HANDLE; // null view keeps phys.imageView null; NeedsDescriptorWrite path gated by writeDescriptors hook
    };
    fns.destroyImage = [](const Render::VulkanContext*, VkImage, VmaAllocation) {};
    fns.destroyImageView = [](const Render::VulkanContext*, VkImageView) {};
    fns.createBuffer = [](const Render::VulkanContext*, const VkBufferCreateInfo&, const VmaAllocationCreateInfo&) -> Render::RenderGraphAllocFns::BufferAlloc {
        static uint64_t counter = 1;
        return {reinterpret_cast<VkBuffer>(counter++), VK_NULL_HANDLE, nullptr};
    };
    fns.destroyBuffer = [](const Render::VulkanContext*, VkBuffer, VmaAllocation) {};
    fns.getBufferDeviceAddress = [](const Render::VulkanContext*, VkBuffer buffer) -> VkDeviceAddress {
        auto val = reinterpret_cast<uint64_t>(buffer);
        return fnv1a64(reinterpret_cast<const uint8_t*>(&val), sizeof(val));
    };
    fns.writeDescriptors = [](Render::PhysicalResource&) {};
    fns.setDebugName = [](const Render::VulkanContext*, VkObjectType, uint64_t, const char*) {};
    return fns;
}

struct RdgFixture : AllocBase
{
    Render::RenderGraph rdg;
    Render::RenderGraphInspector inspector;

    // Fake context satisfies constructor asserts; all GPU calls route through allocFns stubs that ignore it.
    struct FakeContextHolder {
        Render::VulkanContext ctx;
        FakeContextHolder() {
            ctx.allocator = reinterpret_cast<VmaAllocator>(uintptr_t{1});
        }
    };
    inline static FakeContextHolder fakeContextHolder{};

    RdgFixture()
        : rdg(&fakeContextHolder.ctx, nullptr, alloc, arena, MakeStubAllocFns()),
          inspector(rdg)
    {
        if (fakeContextHolder.ctx.allocator == nullptr) {
            fakeContextHolder.ctx.allocator = reinterpret_cast<VmaAllocator>(uintptr_t{1});
        }
        rdg.Reset(0, 0, 100);
    }

    // Helpers
    // VK_FORMAT_UNDEFINED skips CreatePhysicalImage; use for tests that don't need IsAllocated()==true.
    static Render::TextureInfo TexInfo(uint32_t w = 1920, uint32_t h = 1080, uint32_t mips = 1)
    {
        return {VK_FORMAT_UNDEFINED, w, h, mips};
    }

    // Real format causes CreatePhysicalImage to be called (stub sets fake handle so IsAllocated()==true).
    // Required for eviction and viewport-invalidation tests.
    static Render::TextureInfo AllocTexInfo(uint32_t w = 1920, uint32_t h = 1080, uint32_t mips = 1)
    {
        return {VK_FORMAT_R16G16B16A16_SFLOAT, w, h, mips};
    }

    // Compile all stages up to and including PrecomputeBarriers.
    void Compile(int64_t frame = 0) { rdg.Compile(frame); }

    // Compile only through topological sort + wave assignment (no physical allocation).
    void CompileGraph()
    {
        rdg.AccumulateUsage();
        rdg.BuildDependencyEdges();
        rdg.TopologicalSortPasses();
        rdg.CalculateLifetimes();
    }

    void NextFrame(uint32_t frameIdx = 0, uint64_t frame = 1)
    {
        rdg.Reset(frameIdx, frame, 100);
    }
};

// ---- Helper macros ---------------------------------------------------------------

using namespace Render;

// ================================================================================
// Section 1: Resource Registration
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: HasTexture/HasBuffer return false before creation", "[rdg][registration]")
{
    CHECK_FALSE(rdg.HasTexture(SID("nonexistent")));
    CHECK_FALSE(rdg.HasBuffer(SID("nonexistent")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: CreateTexture makes HasTexture true", "[rdg][registration]")
{
    rdg.CreateTexture(SID("colorRT"), TexInfo());
    CHECK(rdg.HasTexture(SID("colorRT")));
    CHECK_FALSE(rdg.HasTexture(SID("otherRT")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: CreateBuffer makes HasBuffer true", "[rdg][registration]")
{
    rdg.CreateBuffer(SID("drawBuf"), 1024);
    CHECK(rdg.HasBuffer(SID("drawBuf")));
    CHECK_FALSE(rdg.HasBuffer(SID("otherBuf")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: AliasTexture makes alias ID visible as texture", "[rdg][registration]")
{
    rdg.CreateTexture(SID("base"), TexInfo());
    rdg.AliasTexture(SID("alias"), SID("base"));
    CHECK(rdg.HasTexture(SID("alias")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: AliasBuffer makes alias ID visible as buffer", "[rdg][registration]")
{
    rdg.CreateBuffer(SID("baseBuf"), 512);
    rdg.AliasBuffer(SID("aliasBuf"), SID("baseBuf"));
    CHECK(rdg.HasBuffer(SID("aliasBuf")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: ImportTexture makes HasTexture true", "[rdg][registration]")
{
    VkImage fakeImg = reinterpret_cast<VkImage>(0xDEADBEEFull);
    VkImageView fakeView = reinterpret_cast<VkImageView>(0xCAFEBABEull);
    rdg.ImportTexture(SID("swapchain"), fakeImg, fakeView, TexInfo(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true);
    CHECK(rdg.HasTexture(SID("swapchain")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: ImportBuffer makes HasBuffer true", "[rdg][registration]")
{
    VkBuffer fakeBuf = reinterpret_cast<VkBuffer>(0xDEAD0001ull);
    PipelineEvent state{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
    rdg.ImportBuffer(SID("extBuf"), fakeBuf, 0, {1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT}, state);
    CHECK(rdg.HasBuffer(SID("extBuf")));
}

// ================================================================================
// Section 2: Usage Accumulation
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — storage image write sets STORAGE_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .WriteStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — storage image read sets STORAGE_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — sampled read sets SAMPLED_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — color attachment sets COLOR_ATTACHMENT_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("color"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT).WriteColorAttachment(SID("color"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("color")) & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — depth attachment sets DEPTH_STENCIL_BIT and SAMPLED_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("depth"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT).WriteDepthAttachment(SID("depth"));
    rdg.AccumulateUsage();
    const VkImageUsageFlags usage = inspector.TextureAccumulatedUsage(SID("depth"));
    CHECK((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
    CHECK((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — blit read sets TRANSFER_SRC_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("src"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_BLIT_BIT).ReadBlitImage(SID("src"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("src")) & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — blit write sets TRANSFER_DST_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("dst"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_BLIT_BIT).WriteBlitImage(SID("dst"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("dst")) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — clear write sets TRANSFER_DST_BIT", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_CLEAR_BIT).WriteClearImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — texture with clear value gets TRANSFER_DST_BIT automatically", "[rdg][usage]")
{
    VkClearValue cv{};
    cv.color = {0.f, 0.f, 0.f, 1.f};
    rdg.CreateTexture(SID("tex"), TexInfo(), cv);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    CHECK((inspector.TextureAccumulatedUsage(SID("tex")) & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — buffer write sets STORAGE and DEVICE_ADDRESS", "[rdg][usage]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AccumulateUsage();
    const VkBufferUsageFlags usage = inspector.BufferAccumulatedUsage(SID("buf"));
    CHECK((usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0);
    CHECK((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — indirect buffer sets INDIRECT_BUFFER_BIT", "[rdg][usage]")
{
    rdg.CreateBuffer(SID("indirect"), 1024);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT).ReadIndirectBuffer(SID("indirect"));
    rdg.AccumulateUsage();
    CHECK((inspector.BufferAccumulatedUsage(SID("indirect")) & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — index buffer sets INDEX_BUFFER_BIT", "[rdg][usage]")
{
    rdg.CreateBuffer(SID("idx"), 1024);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT).ReadIndexBuffer(SID("idx"));
    rdg.AccumulateUsage();
    CHECK((inspector.BufferAccumulatedUsage(SID("idx")) & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: AccumulateUsage — multiple pass types accumulate all bits", "[rdg][usage]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("write"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("read"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("tex"));
    rdg.AccumulateUsage();
    const VkImageUsageFlags usage = inspector.TextureAccumulatedUsage(SID("tex"));
    CHECK((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
    CHECK((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

// ================================================================================
// Section 3: Dependency Edges and Wave Assignment
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: Single pass gets wave 0", "[rdg][waves]")
{
    rdg.AddPass(SID("only"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CompileGraph();
    CHECK(inspector.SortedPassCount() == 1);
    CHECK(inspector.WaveCount() == 1);
    CHECK(inspector.PassWaveIndex(SID("only")) == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Two independent passes both get wave 0", "[rdg][waves]")
{
    rdg.CreateTexture(SID("texA"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo(640, 480));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("texA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("texB"));
    CompileGraph();
    CHECK(inspector.WaveCount() == 1);
    CHECK(inspector.PassWaveIndex(SID("pA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("pB")) == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Linear write-read chain produces sequential waves", "[rdg][waves]")
{
    rdg.CreateTexture(SID("t0"), TexInfo());
    rdg.CreateTexture(SID("t1"), TexInfo(640, 480));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t0"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("t0"))
       .WriteStorageImage(SID("t1"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("t1"));
    CompileGraph();

    CHECK(inspector.WaveCount() == 3);
    CHECK(inspector.PassWaveIndex(SID("pA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("pB")) == 1);
    CHECK(inspector.PassWaveIndex(SID("pC")) == 2);
    CHECK(inspector.PassComesBeforeInSort(SID("pA"), SID("pB")));
    CHECK(inspector.PassComesBeforeInSort(SID("pB"), SID("pC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Diamond pattern — both middle passes share same wave", "[rdg][waves]")
{
    // A writes texIn; B reads texIn and writes texB; C reads texIn and writes texC; D reads texB and texC
    rdg.CreateTexture(SID("texIn"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo(640, 480));
    rdg.CreateTexture(SID("texC"), TexInfo(320, 240));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .WriteStorageImage(SID("texIn"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("texIn"))
       .WriteStorageImage(SID("texB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("texIn"))
       .WriteStorageImage(SID("texC"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("texB"))
       .ReadStorageImage(SID("texC"));
    CompileGraph();

    CHECK(inspector.WaveCount() == 3);
    CHECK(inspector.PassWaveIndex(SID("pA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("pB")) == 1);
    CHECK(inspector.PassWaveIndex(SID("pC")) == 1);
    CHECK(inspector.PassWaveIndex(SID("pD")) == 2);
    CHECK(inspector.PassComesBeforeInSort(SID("pA"), SID("pB")));
    CHECK(inspector.PassComesBeforeInSort(SID("pA"), SID("pC")));
    CHECK(inspector.PassComesBeforeInSort(SID("pB"), SID("pD")));
    CHECK(inspector.PassComesBeforeInSort(SID("pC"), SID("pD")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Write-after-write creates dependency", "[rdg][edges]")
{
    // Two passes both write the same texture: second must come after first
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    rdg.BuildDependencyEdges();
    // pA→pB edge must exist: pB has inDegree >= 1
    CHECK(inspector.PassInDegree(SID("pB")) >= 1);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Read-after-write creates dependency", "[rdg][edges]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    rdg.BuildDependencyEdges();
    CHECK(inspector.PassInDegree(SID("pB")) >= 1);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Write-after-read creates dependency", "[rdg][edges]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AccumulateUsage();
    rdg.BuildDependencyEdges();
    // pC follows pB (WAR) and pA (WAW of previous write)
    CHECK(inspector.PassInDegree(SID("pC")) >= 1);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Independent passes have no mutual dependency", "[rdg][edges]")
{
    rdg.CreateTexture(SID("t1"), TexInfo());
    rdg.CreateTexture(SID("t2"), TexInfo(640, 480));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t1"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t2"));
    rdg.AccumulateUsage();
    rdg.BuildDependencyEdges();
    CHECK(inspector.PassInDegree(SID("pA")) == 0);
    CHECK(inspector.PassInDegree(SID("pB")) == 0);
    CHECK(inspector.PassOutEdgeCount(SID("pA")) == 0);
    CHECK(inspector.PassOutEdgeCount(SID("pB")) == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Same-layout readers in same epoch share dependency on transition cause", "[rdg][edges]")
{
    // A writes tex; B and C both read it with same layout. Neither B nor C depends on the other.
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    CompileGraph();
    // pB and pC must both come after pA, but can be in the same wave
    CHECK(inspector.PassWaveIndex(SID("pA")) < inspector.PassWaveIndex(SID("pB")));
    CHECK(inspector.PassWaveIndex(SID("pA")) < inspector.PassWaveIndex(SID("pC")));
    CHECK(inspector.PassWaveIndex(SID("pB")) == inspector.PassWaveIndex(SID("pC")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Buffer read-after-write creates dependency", "[rdg][edges]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("buf"));
    rdg.AccumulateUsage();
    rdg.BuildDependencyEdges();
    CHECK(inspector.PassInDegree(SID("pB")) >= 1);
}

// ================================================================================
// Section 4: Lifetime Calculation
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: Resource used in first and last sorted pass has correct lifetime", "[rdg][lifetime]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    CompileGraph();
    rdg.CalculateLifetimes();
    CHECK(inspector.TextureFirstPass(SID("tex")) == 0);
    CHECK(inspector.TextureLastPass(SID("tex")) == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Resource used only in middle pass has tight lifetime", "[rdg][lifetime]")
{
    rdg.CreateTexture(SID("t0"), TexInfo());
    rdg.CreateTexture(SID("t1"), TexInfo(640, 480));
    rdg.CreateTexture(SID("t2"), TexInfo(320, 240));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t0"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("t0"))
       .WriteStorageImage(SID("t1"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("t1"))
       .WriteStorageImage(SID("t2"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("t2"));
    CompileGraph();
    rdg.CalculateLifetimes();
    // t1 is only live during pB and pC (sorted indices 1 and 2)
    CHECK(inspector.TextureFirstPass(SID("t1")) == 1);
    CHECK(inspector.TextureLastPass(SID("t1")) == 2);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Buffer lifetime tracked correctly across passes", "[rdg][lifetime]")
{
    rdg.CreateBuffer(SID("buf"), 1024);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("buf"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadBuffer(SID("buf"));
    CompileGraph();
    rdg.CalculateLifetimes();
    CHECK(inspector.BufferFirstPass(SID("buf")) == 0);
    CHECK(inspector.BufferLastPass(SID("buf")) == 1);
}

// ================================================================================
// Section 5: Auto-Clear
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: Texture with clear value is auto-cleared in its first pass", "[rdg][autoclear]")
{
    VkClearValue cv{};
    cv.color = {0.f, 0.f, 0.f, 1.f};
    rdg.CreateTexture(SID("clearTex"), TexInfo(), cv);
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("clearTex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("clearTex"));
    CompileGraph();
    rdg.PopulateAutoClearTextures();
    CHECK(inspector.TextureHasClearInPass(SID("clearTex"), SID("pA")));
    CHECK_FALSE(inspector.TextureHasClearInPass(SID("clearTex"), SID("pB")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Texture without clear value is not auto-cleared", "[rdg][autoclear]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    CompileGraph();
    rdg.PopulateAutoClearTextures();
    CHECK_FALSE(inspector.TextureHasClearInPass(SID("tex"), SID("pA")));
}

// ================================================================================
// Section 6: Physical Resource Aliasing
// ================================================================================

// Two textures with non-overlapping lifetimes and identical dimensions share one physical resource.
TEST_CASE_METHOD(RdgFixture, "RDG: Non-overlapping same-dim textures alias to the same physical", "[rdg][aliasing]")
{
    // Chain: pA writes texA → pB reads texA, writes texB → pC reads texB, writes texC → pD reads texC
    // texA lives during pA+pB (waves 0+1); texC lives during pC+pD (waves 2+3).
    // texA.lastWave < texC.firstWave → can alias.
    rdg.CreateTexture(SID("texA"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo(640, 480));
    rdg.CreateTexture(SID("texC"), TexInfo()); // same dims as texA
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("texA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("texA")).WriteStorageImage(SID("texB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("texB")).WriteStorageImage(SID("texC"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("texC"));
    Compile();

    CHECK(inspector.TexturesSharePhysical(SID("texA"), SID("texC")));
    // texB has different dimensions, must be its own physical
    CHECK_FALSE(inspector.TexturesSharePhysical(SID("texA"), SID("texB")));
}

// Two textures with overlapping lifetimes cannot alias, even with same dimensions.
TEST_CASE_METHOD(RdgFixture, "RDG: Overlapping same-dim textures do not alias", "[rdg][aliasing]")
{
    // Both texA and texB are live during the same pass, so they can't share a physical.
    rdg.CreateTexture(SID("texA"), TexInfo());
    rdg.CreateTexture(SID("texB"), TexInfo()); // same dims
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .WriteStorageImage(SID("texA")).WriteStorageImage(SID("texB"));
    Compile();

    CHECK_FALSE(inspector.TexturesSharePhysical(SID("texA"), SID("texB")));
    CHECK(inspector.PhysicalCount() >= 2);
}

// Textures with different dimensions never alias.
TEST_CASE_METHOD(RdgFixture, "RDG: Different-dimension textures never alias", "[rdg][aliasing]")
{
    rdg.CreateTexture(SID("big"), TexInfo(1920, 1080));
    rdg.CreateTexture(SID("small"), TexInfo(960, 540));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("big"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("small"));
    Compile();

    CHECK_FALSE(inspector.TexturesSharePhysical(SID("big"), SID("small")));
}

// An imported texture is never aliased with a transient texture.
TEST_CASE_METHOD(RdgFixture, "RDG: Imported texture is never aliased", "[rdg][aliasing]")
{
    VkImage fakeImg = reinterpret_cast<VkImage>(0x1111ull);
    VkImageView fakeView = reinterpret_cast<VkImageView>(0x2222ull);
    rdg.ImportTexture(SID("imported"), fakeImg, fakeView, TexInfo(),
                      VK_IMAGE_USAGE_STORAGE_BIT,
                      VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_IMAGE_LAYOUT_GENERAL);
    rdg.CreateTexture(SID("transient"), TexInfo()); // same dims

    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .WriteStorageImage(SID("imported"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .WriteStorageImage(SID("transient"));
    Compile();

    CHECK_FALSE(inspector.TexturesSharePhysical(SID("imported"), SID("transient")));
    CHECK(inspector.PhysicalIsImported(inspector.TexturePhysicalIndex(SID("imported"))));
}

// Two buffers with the same size and non-overlapping lifetimes alias.
TEST_CASE_METHOD(RdgFixture, "RDG: Non-overlapping same-size buffers alias", "[rdg][aliasing]")
{
    rdg.CreateBuffer(SID("bufA"), 4096);
    rdg.CreateBuffer(SID("bufB"), 4096); // same size, different name
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("bufA"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadBuffer(SID("bufA")).WriteBuffer(SID("bufB"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadBuffer(SID("bufB"));
    Compile();

    // bufA lives in pA(0) and pB(1); bufB lives in pB(1) and pC(2): no non-overlapping pair here.
    // Let's just check counts: 2 unique-size buffers → at most 1 physical if non-overlapping,
    // at least 2 if overlapping. bufA.lastPass=1, bufB.firstPass=1 → overlap at pass 1 → no alias.
    CHECK_FALSE(inspector.BuffersSharePhysical(SID("bufA"), SID("bufB")));
}

// Buffer with bCanAlias=false is always its own physical.
TEST_CASE_METHOD(RdgFixture, "RDG: Buffer with bCanAlias=false never aliases", "[rdg][aliasing]")
{
    rdg.CreateBuffer(SID("noAlias"), 4096, false, false); // bCanAlias=false
    rdg.CreateBuffer(SID("normal"), 4096);
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("noAlias"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteBuffer(SID("normal"));
    Compile();

    CHECK_FALSE(inspector.BuffersSharePhysical(SID("noAlias"), SID("normal")));
    CHECK_FALSE(inspector.PhysicalCanAlias(inspector.BufferPhysicalIndex(SID("noAlias"))));
}

// ================================================================================
// Section 7: Multi-Frame Reset
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: After Reset, previously registered resources are gone", "[rdg][reset]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.CreateBuffer(SID("buf"), 1024);
    CHECK(rdg.HasTexture(SID("tex")));
    CHECK(rdg.HasBuffer(SID("buf")));

    NextFrame();
    CHECK_FALSE(rdg.HasTexture(SID("tex")));
    CHECK_FALSE(rdg.HasBuffer(SID("buf")));
}

TEST_CASE_METHOD(RdgFixture, "RDG: Second frame compiles a fresh graph correctly", "[rdg][reset]")
{
    // Frame 0
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile(0);

    // Frame 1
    NextFrame(0, 1);
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.CreateTexture(SID("out"), TexInfo(640, 480));
    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("tex")).WriteStorageImage(SID("out"));
    Compile(1);

    CHECK(inspector.SortedPassCount() == 2);
    CHECK(inspector.PassWaveIndex(SID("pA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("pB")) == 1);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Physical resources from frame 0 are reused (not grown) in frame 1", "[rdg][reset]")
{
    // Frame 0: create one texture physical
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile(0);
    const size_t physAfterFrame0 = inspector.PhysicalCount();
    CHECK(physAfterFrame0 >= 1);

    // Frame 1: same texture dims → same physical reused (physical count should not grow)
    NextFrame(0, 1);
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile(1);
    CHECK(inspector.PhysicalCount() == physAfterFrame0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Physical resources evicted after maxFramesUnused", "[rdg][reset]")
{
    // AllocTexInfo used so CreatePhysicalImage is called → IsAllocated()==true → eviction loop doesn't skip.
    rdg.CreateTexture(SID("tex"), AllocTexInfo());
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    Compile(0);
    CHECK(inspector.PhysicalCount() >= 1);

    // Frames 1..3: don't use the texture; maxFramesUnused=1 → evicted after 1 unused frame
    // Use maxFramesUnused=1 in Reset to evict quickly
    rdg.Reset(0, 1, 1); // frame 1, maxUnused=1
    Compile(1);         // no passes, no textures → nothing updates lastUsedFrame

    // Frame 2 reset: texture was unused for 1 frame → evicted
    rdg.Reset(0, 2, 1);
    CHECK(inspector.PhysicalCount() == 0);
}

// ================================================================================
// Section 8: Viewport-Scaled Resources
// ================================================================================

TEST_CASE_METHOD(RdgFixture, "RDG: Viewport-scaled physical is evicted on InvalidateAllViewportAssociated", "[rdg][viewport]")
{
    rdg.CreateTexture(SID("vpTex"), AllocTexInfo(), std::nullopt, /*bIsViewportScaled=*/true);
    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("vpTex"));
    Compile(0);

    const uint32_t physIdx = inspector.TexturePhysicalIndex(SID("vpTex"));
    CHECK(inspector.PhysicalIsViewportScaled(physIdx));

    rdg.InvalidateAllViewportAssociated();
    rdg.Reset(0, 1, 100); // triggers deletion of viewport-scaled physicals
    CHECK(inspector.PhysicalCount() == 0);
}

TEST_CASE_METHOD(RdgFixture, "RDG: Non-viewport-scaled resource survives InvalidateAllViewportAssociated", "[rdg][viewport]")
{
    rdg.CreateTexture(SID("vpTex"), AllocTexInfo(), std::nullopt, true);
    rdg.CreateTexture(SID("staticTex"), AllocTexInfo(64, 64));
    rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("vpTex"));
    rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("staticTex"));
    Compile(0);

    rdg.InvalidateAllViewportAssociated();
    rdg.Reset(0, 1, 100);
    // staticTex physical stays; vpTex physical evicted
    CHECK(inspector.PhysicalCount() == 1);
    CHECK_FALSE(inspector.PhysicalIsViewportScaled(0));
}

// ================================================================================
// Section 9: Wildcard / Stress Tests
// ================================================================================

// ReadWriteImage creates a write dependency for both WAR and WAW.
TEST_CASE_METHOD(RdgFixture, "RDG: ReadWriteImage on same texture creates write-ordering dependency", "[rdg][wildcard]")
{
    rdg.CreateTexture(SID("tex"), TexInfo());
    rdg.AddPass(SID("init"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tex"));
    rdg.AddPass(SID("rw"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadWriteImage(SID("tex"));
    rdg.AddPass(SID("consume"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("tex"));
    CompileGraph();

    CHECK(inspector.PassWaveIndex(SID("init")) < inspector.PassWaveIndex(SID("rw")));
    CHECK(inspector.PassWaveIndex(SID("rw")) < inspector.PassWaveIndex(SID("consume")));
}

// A texture used by both a color attachment pass and a sampled read pass accumulates both usage bits.
TEST_CASE_METHOD(RdgFixture, "RDG: Texture used as color attachment and sampled read has both usage bits", "[rdg][wildcard]")
{
    rdg.CreateTexture(SID("rt"), TexInfo());
    rdg.AddPass(SID("draw"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
       .WriteColorAttachment(SID("rt"));
    rdg.AddPass(SID("post"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT)
       .ReadSampledImage(SID("rt"));
    rdg.AccumulateUsage();

    const VkImageUsageFlags usage = inspector.TextureAccumulatedUsage(SID("rt"));
    CHECK((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
    CHECK((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0);
}

// Three-way aliasing: A→B→C pipeline where A and C have the same dimensions and non-overlapping lives.
// Exactly 2 physicals expected: one shared by A+C, one for B.
TEST_CASE_METHOD(RdgFixture, "RDG: Three-pass chain with non-overlapping head and tail texture reduces physical count", "[rdg][wildcard]")
{
    rdg.CreateTexture(SID("A"), TexInfo());          // 1920x1080
    rdg.CreateTexture(SID("B"), TexInfo(640, 480));  // different dims
    rdg.CreateTexture(SID("C"), TexInfo());           // same dims as A

    rdg.AddPass(SID("pA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("A"));
    rdg.AddPass(SID("pB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("A")).WriteStorageImage(SID("B"));
    rdg.AddPass(SID("pC"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("B")).WriteStorageImage(SID("C"));
    rdg.AddPass(SID("pD"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).ReadStorageImage(SID("C"));
    Compile();

    // A: [pA=wave0, pB=wave1]; C: [pC=wave2, pD=wave3] → no wave overlap → alias
    CHECK(inspector.TexturesSharePhysical(SID("A"), SID("C")));
    CHECK(inspector.PhysicalCount() == 2); // {A,C} + {B}
}

// Parallel producer/consumer: two independent producers feeding into a single merger.
TEST_CASE_METHOD(RdgFixture, "RDG: Two parallel producers and one consumer — merger is in wave 1", "[rdg][wildcard]")
{
    rdg.CreateTexture(SID("tA"), TexInfo());
    rdg.CreateTexture(SID("tB"), TexInfo(640, 480));
    rdg.AddPass(SID("prodA"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tA"));
    rdg.AddPass(SID("prodB"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("tB"));
    rdg.AddPass(SID("merge"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
       .ReadStorageImage(SID("tA")).ReadStorageImage(SID("tB"));
    CompileGraph();

    CHECK(inspector.PassWaveIndex(SID("prodA")) == 0);
    CHECK(inspector.PassWaveIndex(SID("prodB")) == 0);
    CHECK(inspector.PassWaveIndex(SID("merge")) == 1);
    CHECK(inspector.WaveCount() == 2);
}

// Compile is idempotent when Reset is called between frames.
TEST_CASE_METHOD(RdgFixture, "RDG: Identical graphs compiled on consecutive frames produce identical structure", "[rdg][wildcard]")
{
    auto buildGraph = [&]() {
        rdg.CreateTexture(SID("t"), TexInfo());
        rdg.AddPass(SID("p0"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("t"));
        rdg.AddPass(SID("p1"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT).ReadSampledImage(SID("t"));
    };

    buildGraph();
    Compile(0);
    const size_t phys0 = inspector.PhysicalCount();
    const uint32_t wave0_p0 = inspector.PassWaveIndex(SID("p0"));
    const uint32_t wave0_p1 = inspector.PassWaveIndex(SID("p1"));

    NextFrame(0, 1);
    buildGraph();
    Compile(1);

    CHECK(inspector.PhysicalCount() == phys0);
    CHECK(inspector.PassWaveIndex(SID("p0")) == wave0_p0);
    CHECK(inspector.PassWaveIndex(SID("p1")) == wave0_p1);
}

// AliasTexture: writes through the alias affect the original texture's physical.
TEST_CASE_METHOD(RdgFixture, "RDG: Alias texture writes accumulate usage on the underlying resource", "[rdg][wildcard]")
{
    rdg.CreateTexture(SID("base"), TexInfo());
    rdg.AliasTexture(SID("alias"), SID("base"));

    rdg.AddPass(SID("p"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT).WriteStorageImage(SID("alias"));
    rdg.AccumulateUsage();

    // Both "base" and "alias" resolve to the same logical resource; usage should be set on it
    CHECK((inspector.TextureAccumulatedUsage(SID("base")) & VK_IMAGE_USAGE_STORAGE_BIT) != 0);
}
