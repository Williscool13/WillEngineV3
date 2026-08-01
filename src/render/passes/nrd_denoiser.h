//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_NRD_DENOISER_H
#define WILL_ENGINE_NRD_DENOISER_H

#include <volk.h>

#include <NRD.h>

#include "core/containers/array.h"
#include "core/containers/vector.h"
#include "core/string_id.h"
#include "render/renderer_types.h"
#include "render/interface/render_interface.h"
#include "render/vulkan/vk_resources.h"

namespace Core { struct ViewFamily; }

namespace Render
{
class PipelineManager;
class RenderGraph;
class ResourceManager;
struct VulkanContext;

inline constexpr uint32_t NRD_RELAX_IDENTIFIER = 1;
inline constexpr uint32_t NRD_REBLUR_IDENTIFIER = 2;

enum class NrdBackend : uint32_t { Relax = 0, Reblur = 1 };

/**
 * Reference integration of the real NRD library (RELAX_DIFFUSE_SPECULAR + REBLUR_DIFFUSE_SPECULAR) via its raw API.
 * Owns the nrd::Instance, all pool/IO textures, per-pipeline layouts/pipelines, samplers, per-frame descriptor pools and the constant ring buffer. Records all NRD dispatches inside a single RDG pass and issues manual barriers between dispatches (mirrors NRDIntegration).
 */
class NrdDenoiser
{
public:
    NrdDenoiser(VulkanContext* context, Core::TlsfAllocator& renderAlloc);

    ~NrdDenoiser();

    NrdDenoiser(const NrdDenoiser&) = delete;

    NrdDenoiser& operator=(const NrdDenoiser&) = delete;

    /**
     * Ensures instance + resources exist, imports the IO textures into the graph and stages settings.
     * Call before the prep passes; AddDispatchPass must be added after them (RDG edges follow declaration order).
     * @return false if NRD initialization failed; callers must skip the prep/dispatch/writeback passes then.
     */
    bool Prepare(RenderGraph& graph,
                 const Core::ViewFamily& viewFamily,
                 Core::Array<uint32_t, 2> renderExtent,
                 NrdBackend backend,
                 const Core::RELAXParams& relaxParams,
                 const Core::ReBLURParams& reblurParams,
                 uint64_t frameNumber,
                 uint32_t frameInFlightIndex,
                 float renderFps);

    /**
     * Adds the single NRD dispatch pass for the active backend; call between the prep passes and the output writeback.
     * Binding NRD's classic descriptor sets invalidates the engine's descriptor buffer bindings, so the pass rebinds them after the dispatches.
     */
    void AddDispatchPass(RenderGraph& graph, ResourceManager* resourceManager, PipelineManager* pipelineManager, uint32_t frameInFlightIndex);

private:
    enum IoTexture : uint32_t
    {
        IO_IN_MV = 0,
        IO_IN_NORMAL_ROUGHNESS,
        IO_IN_VIEWZ,
        IO_IN_DIFF_RADIANCE_HITDIST,
        IO_IN_SPEC_RADIANCE_HITDIST,
        IO_OUT_DIFF_RADIANCE_HITDIST,
        IO_OUT_SPEC_RADIANCE_HITDIST,
        IO_COUNT
    };

    struct TrackedTexture
    {
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        uint32_t width{0};
        uint32_t height{0};
        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkAccessFlags2 access{VK_ACCESS_2_NONE};
    };

    struct PipelineObjects
    {
        VkDescriptorSetLayout setLayout{VK_NULL_HANDLE};
        VkPipelineLayout layout{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
    };

    struct RetiredImage
    {
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        uint64_t retiredFrame{0};
    };

    bool EnsureInstance();

    void EnsureResources(Core::Array<uint32_t, 2> renderExtent, uint64_t frameNumber);

    void ReleaseRetired(uint64_t frameNumber, bool bForce);

    void StageSettings(const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const Core::RELAXParams& relaxParams, const Core::ReBLURParams& reblurParams, uint64_t frameNumber, float renderFps, bool bHistoryReset);

    void RecordDispatches(VkCommandBuffer cmd, ResourceManager* resourceManager, PipelineManager* pipelineManager, uint32_t frameInFlightIndex);

    void RebindEngineDescriptorBuffers(VkCommandBuffer cmd, ResourceManager* resourceManager, PipelineManager* pipelineManager);

    TrackedTexture* ResolveResource(const nrd::ResourceDesc& resource);

    bool CreateTrackedTexture(TrackedTexture& tex, VkFormat format, uint32_t width, uint32_t height, const char* debugName);

    void RetireTrackedTexture(TrackedTexture& tex, uint64_t frameNumber);

private:
    // Non-Owning
    VulkanContext* context{nullptr};
    Core::TlsfAllocator* alloc{nullptr};

    // Owning
    nrd::Instance* instance{nullptr};
    Core::Vector<PipelineObjects> pipelines;
    Core::Vector<VkSampler> samplers;
    VkDescriptorSetLayout sharedSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool sharedDescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet sharedSet{VK_NULL_HANDLE};
    Core::Array<VkDescriptorPool, Core::FRAME_BUFFER_COUNT> frameDescriptorPools{};
    AllocatedBuffer constantRing{};
    uint32_t constantStride{0};
    uint32_t constantSlotsPerFrame{0};

    Core::Vector<TrackedTexture> poolTextures;
    Core::Array<TrackedTexture, IO_COUNT> ioTextures{};
    Core::Vector<RetiredImage> retiredImages;

    uint32_t currentWidth{0};
    uint32_t currentHeight{0};
    uint32_t prevRectWidth{0};
    uint32_t prevRectHeight{0};
    uint64_t lastRecordedFrame{UINT64_MAX};
    bool bInitialized{false};
    bool bInitFailed{false};
    bool bPendingHistoryClear{false};
    NrdBackend activeBackend{NrdBackend::Relax};
    NrdBackend lastBackend{NrdBackend::Relax};

    nrd::CommonSettings stagedCommon{};
    nrd::RelaxSettings stagedRelax{};
    nrd::ReblurSettings stagedReblur{};
};

/** Fills IN_NORMAL_ROUGHNESS / IN_MV / IN_VIEWZ from gbuffer_one + depth, and IN_DIFF/IN_SPEC from the demodulated intermediates (ReBLUR backend: YCoCg + normalized hitT front-end packing). */
void SetupNRDPrepPasses(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, NrdBackend backend, const Core::ReBLURParams& reblurParams);

/** Copies OUT_DIFF/OUT_SPEC back into the intermediates the engine remodulate pass consumes (ReBLUR backend: YCoCg back to linear). */
void SetupNRDOutputPass(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, NrdBackend backend);
} // Render

#endif //WILL_ENGINE_NRD_DENOISER_H
