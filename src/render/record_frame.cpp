//
// Created by William on 2026-10-05.
//

#include "render_thread.h"

#include <cstring>
#include <chrono>
#include <ctime>
#include <glm/gtc/packing.hpp>
#include <tracy/Tracy.hpp>

#include "record_frame_context.h"
#include "renderer.h"
#include "render_utils.h"
#include "gpu_dispatcher.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_render_extents.h"
#include "render/vulkan/vk_utils.h"
#include "resource_manager.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render-graph/render_graph.h"
#include "render-graph/render_pass.h"
#include "shaders/constants_interop.h"
#include "shaders/push_constant_interop.h"
#include "shaders/flags_interop.h"
#include "types/render_types.h"
#include "core/containers/inline_string.h"
#include "core/containers/span.h"
#include "core/string_id.h"
#include "core/math/math_helpers.h"
#include "engine/logging/engine_log.h"
#include "pipelines/pipeline_manager.h"
#include "render-view/render_view_helpers.h"
#include "post-processing/post_processing.h"
#include "vulkan/vk_config.h"

#if WILL_EDITOR
#include "editor/renderer/debug_readback_buffer.h"
#include "shaders/instancing_interop.h"
#endif

namespace Render
{
static bool DisplayUvToRenderPixel(float u, float v, const PaniniParams& panini, float aspect, Core::Array<uint32_t, 2> renderExtent, Core::Array<uint32_t, 2>& outPixel)
{
    if (!PaniniDisplayToSourceUv(panini, aspect, u, v)) { return false; }
    outPixel[0] = std::min(renderExtent[0] - 1, static_cast<uint32_t>(u * static_cast<float>(renderExtent[0])));
    outPixel[1] = std::min(renderExtent[1] - 1, static_cast<uint32_t>(v * static_cast<float>(renderExtent[1])));
    return true;
}

static void AddColorCopyPass(RenderGraph& graph, PipelineManager* pipelineManager, StringID passName, StringID src, StringID dst, Core::Array<uint32_t, 2> extent)
{
    auto& copyPass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged);
    copyPass.ReadSampledImage(src);
    copyPass.WriteStorageImage(dst);
    copyPass.Execute([src, dst, w = extent[0], h = extent[1], pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("color_copy"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
        ColorCopyPushConstant pc{
            .srcIndex = graph.GetSampledImageViewDescriptorIndex(src),
            .dstIndex = graph.GetStorageImageViewDescriptorIndex(dst),
            .extents = {w, h},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
    });
}

static FrameRenderingPath ResolveFrameRenderingPath(const Core::ViewFamily& viewFamily)
{
    if (viewFamily.groundTruthMode != Core::GroundTruthMode::None) {
        return FrameRenderingPath::GroundTruth;
    }
    switch (viewFamily.lightingMode) {
        case Core::LightingMode::Default: return FrameRenderingPath::Default;
        case Core::LightingMode::ReSTIR: return FrameRenderingPath::ReSTIR;
        case Core::LightingMode::PathTracing: return FrameRenderingPath::PathTracing;
    }
    return FrameRenderingPath::Default;
}

static FrameFeatures ComputeFrameFeatures(const Core::FrameBuffer& frameBuffer, const Core::ViewFamily& viewFamily, FrameRenderingPath path, bool bHasScene)
{
    FrameFeatures f{};
    if (!bHasScene || path == FrameRenderingPath::GroundTruth) {
        return f;
    }

    const Core::DDGIParams& ddgi = frameBuffer.ddgi;
    f.ddgi = !ddgi.bEnabled ? DDGIUsage::Off : ddgi.bApplyToLighting ? DDGIUsage::Applied : DDGIUsage::ProbesOnly;
    f.bGIGather = ddgi.bEnabled && ((ddgi.bFinalGather && f.ddgi == DDGIUsage::Applied) || frameBuffer.debug.giGatherDebugMode != 0);
    f.bGTAO = viewFamily.gtaoConfig.bEnabled;
    if (path == FrameRenderingPath::PathTracing) {
        return f;
    }

    f.bVolumetricFog = viewFamily.volumetricFog.bEnabled;
    if (path == FrameRenderingPath::ReSTIR && frameBuffer.restir.bSunLight) {
        f.sunShadow = SunShadowSource::ReSTIR;
    }
    else if (viewFamily.directionalLight.bEnabled && viewFamily.directionalLight.intensity > 0.0f) {
        f.sunShadow = SunShadowSource::RayTraced;
    }
    return f;
}

static FrameNeeds ComputeFrameNeeds(const Core::FrameBuffer& frameBuffer, const Core::ViewFamily& viewFamily, FrameRenderingPath path, const FrameFeatures& features, bool bCanRender, bool bHasScene)
{
    FrameNeeds n{};
    if (!bCanRender) {
        return n;
    }
    const bool bReSTIR = path == FrameRenderingPath::ReSTIR;
    if (bHasScene && path != FrameRenderingPath::GroundTruth) {
        n.bWorldGrid = path == FrameRenderingPath::Default
                       || (bReSTIR && (frameBuffer.reflection.bEnabled || frameBuffer.restir.lightProposal == Core::ReSTIRParams::LightProposal::WorldGridBin))
                       || features.ddgi != DDGIUsage::Off
                       || viewFamily.reflectionProbes.Size() > 0u
                       || features.bVolumetricFog;
    }

    // A remodulate debug output is not the lit scene, so it must not feed back as history
    const bool bLitColorIsScene = (path == FrameRenderingPath::Default || bReSTIR) && frameBuffer.restir.remodulateOutput == Core::ReSTIRParams::RemodulateOutput::Both;
    const bool bReflectionScreenSpace = bReSTIR && frameBuffer.reflection.bEnabled && frameBuffer.reflection.bScreenSpaceLighting;
    n.bLitHistory = bHasScene && bLitColorIsScene && (bReflectionScreenSpace || features.bGIGather);

    const bool bFsr2Reactive = viewFamily.aaConfig.mode == Core::AntiAliasingMode::FSR2 && viewFamily.aaConfig.fsr2.bReactiveMask;
    n.bPreOverlayColor = bFsr2Reactive || viewFamily.postProcessConfig.exposureMode == Core::ExposureMode::Auto;
    return n;
}

RenderThread::RenderResponseCode RenderThread::RecordFrame(uint32_t frameIndex, VkCommandBuffer cmd, VkCommandBuffer asyncCmd, Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot)
{
    ZoneScoped;

    ApplyRenderReset(frameBuffer.renderReset);

    renderGraph->FrameStartReset(frameIndex, frameNumber, RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD);

    FrameContext ctx = BeginFrame(frameIndex, frameBuffer);
    RecordFrameSetup(ctx, cmd, asyncCmd);

    if (ctx.bCanRender) {
        ZoneScopedN("SetupRenderGraph");
        RecordSceneServices(ctx);
        if (ctx.bHasScene) {
            RecordLighting(ctx);
        }
        RecordPostLighting(ctx);
        RecordPresentation(ctx);
#if WILL_EDITOR
        RecordDiagnostics(ctx);
#endif
    }

    RecordFrameExport(ctx);

    {
        ZoneScopedN("RenderGraphCompile");
        renderGraph->SetDebugLogging(frameBuffer.bLogRDG);
        if (renderGraph->IsForceGraphicsQueue() && !frameBuffer.debug.bDisableAsyncCompute) {
            vkQueueWaitIdle(context->graphicsQueue);
            gpuDispatcher->WaitAsyncComputeIdle();
            renderGraph->ClearGraphicsFrameStamps();
        }
        renderGraph->SetForceGraphicsQueue(frameBuffer.debug.bDisableAsyncCompute);
#ifdef ENABLE_VULKAN_VALIDATION
        if (frameBuffer.bLogRDG) {
            pipelineManager->DumpExecutableStats(Platform::GetAssetPath() / "visualizations" / "pipeline_executable_stats.txt");
        }
#endif
        renderGraph->Compile(frameNumber);
        if (bVRAMReportShouldWrite.load(std::memory_order_relaxed)) {
            bVRAMReportShouldWrite.store(false, std::memory_order_relaxed);
            vramReport = renderGraph->GenerateVramReport();
            bVRAMReportShouldRead.store(true, std::memory_order_release);
        }
    } {
        ZoneScopedN("RenderGraphExecute");
        renderGraph->Execute(asyncCmd, cmd);
    }

#if WILL_EDITOR
    resourceManager->debugReadback.SetLastKnownState(renderGraph->GetBufferState("debug_readback_buffer"_sid));
#endif
    return bRenderRequestsRecreate ? RENDER_REQUESTED_RECREATE : SUCCESS;
}

void RenderThread::ApplyRenderReset(Core::RenderReset reset)
{
    if (reset != Core::RenderReset::None) {
        nrdDenoiser->RequestHistoryClear();
    }
    if (reset == Core::RenderReset::Cut) {
        renderGraph->InvalidateViewportHistory();
    }
    else if (reset == Core::RenderReset::Flush) {
        vkQueueWaitIdle(context->graphicsQueue);
        gpuDispatcher->WaitAsyncComputeIdle();
        renderGraph->InvalidateAllVersioned();
        renderGraph->ClearReadbacks();
        frameResourceLimits = FrameResourceLimits{};
        rtGroundTruthDIAccumCount = 0;
        rtGroundTruthGIAccumCount = 0;
        rtGroundTruthFullAccumCount = 0;
        previousRestirCheckerboardField = 0;
        previousRestirFullRateResolve = false;
        ddgiPreviousCascades = DDGICascades{};
    }
}

FrameContext RenderThread::BeginFrame(uint32_t frameIndex, Core::FrameBuffer& frameBuffer)
{
    Core::ViewFamily& viewFamily = frameBuffer.mainViewFamily;

    FrameContext ctx{
        .frameBuffer = frameBuffer,
        .viewFamily = viewFamily,
        .frameIndex = frameIndex,
        .renderExtent = renderExtents->GetScaledExtent(),
        .outputExtent = renderExtents->GetViewportExtent(),
    };
    ctx.postAaExtent = ctx.renderExtent;
    ctx.displayAspect = static_cast<float>(ctx.outputExtent[0]) / static_cast<float>(ctx.outputExtent[1]);
    ctx.displayPanini = viewFamily.debugResourceName.IsEmpty()
        ? ComputePaniniParams(viewFamily.postProcessConfig, viewFamily.mainView.currentViewData.fovRadians, ctx.displayAspect)
        : PaniniParams{};

    // GPU->CPU Readback
    ReadbackStruct* readbackData = renderGraph->GetReadbackData();
    {
        frameBuffer.stableIdUnderCursor = readbackData->selectedStableId;
        statisticsManager.scratch.visibleMeshletCount = readbackData->meshletCount;
        statisticsManager.scratch.culledInstanceFrustum = readbackData->culledInstanceFrustum;
        statisticsManager.scratch.culledInstanceContribution = readbackData->culledInstanceContribution;
        statisticsManager.scratch.culledInstanceOcclusion = readbackData->culledInstanceOcclusion;
        statisticsManager.scratch.culledMeshletFrustum = readbackData->culledMeshletFrustum;
        statisticsManager.scratch.culledMeshletCone = readbackData->culledMeshletCone;
        statisticsManager.scratch.culledMeshletContribution = readbackData->culledMeshletContribution;
        statisticsManager.scratch.culledMeshletOcclusion = readbackData->culledMeshletOcclusion;
        for (uint32_t r = 0; r < 4; r++) {
            statisticsManager.scratch.meshletRegionExpanded[r] = readbackData->meshletRegionExpanded[r];
            statisticsManager.scratch.meshletRegionVisible[r] = readbackData->meshletRegionVisible[r];
        }
        statisticsManager.scratch.shadingDispatches = readbackData->shadingDispatches;

        const Core::PostProcessConfiguration& ppConfig = viewFamily.postProcessConfig;
        prevPreExposure = preExposure;
        if (ppConfig.exposureMode != Core::ExposureMode::Auto) {
            preExposure = ppConfig.exposureTargetLuminance / EV100ToLuminance(CameraEV100(ppConfig));
        }
        else {
            const float minLuminance = EV100ToLuminance(ppConfig.exposureMinEV100);
            const float maxLuminance = std::max(minLuminance, EV100ToLuminance(ppConfig.exposureMaxEV100));
            const float adaptedLuminance = readbackData->adaptedLuminance > 0.0f ? readbackData->adaptedLuminance : minLuminance;
            preExposure = ppConfig.exposureTargetLuminance / std::clamp(adaptedLuminance, minLuminance, maxLuminance);
        }
        const float renderFps = frameBuffer.timeFrame.renderFps;
        const float autoFramerateScale = glm::clamp(renderFps > 0.0f ? renderFps / 60.0f : 1.0f, 0.25f, 4.0f);
        framerateScale = frameBuffer.debug.framerateScaleOverride > 0.0f ? frameBuffer.debug.framerateScaleOverride : autoFramerateScale;
        statisticsManager.scratch.lightingDispatches = readbackData->lightingDispatches;
        statisticsManager.scratch.radianceCache.occupiedSlots = readbackData->wcOccupied;
        statisticsManager.scratch.radianceCache.cellsCarried = readbackData->wcCarried;
        statisticsManager.scratch.radianceCache.cellsEvicted = readbackData->wcEvicted;
        statisticsManager.scratch.radianceCache.insertsFailed = readbackData->wcInsertsFailed;
        statisticsManager.scratch.radianceCache.cellsShaded = readbackData->wcShaded;
        statisticsManager.scratch.regir.activeCells = readbackData->regirActiveCells;
        statisticsManager.scratch.regir.insertsFailed = readbackData->regirInsertsFailed;
        statisticsManager.scratch.regir.gatherOverflow = readbackData->regirGatherOverflow;
        statisticsManager.scratch.regir.coneRejected = readbackData->regirConeRejected;
        static_assert(sizeof(ReGIRCursorCell) == offsetof(ReadbackStruct, regirCursorTopPos) + sizeof(float) * 24 - offsetof(ReadbackStruct, regirCursorValid));
        std::memcpy(&statisticsManager.scratch.regir.cursor, &readbackData->regirCursorValid, sizeof(ReGIRCursorCell));
        static_assert(sizeof(WorldGridCursorCell) == offsetof(ReadbackStruct, wgCursorTopMeshletCenter) + sizeof(float) * 24 - offsetof(ReadbackStruct, wgCursorValid));
        std::memcpy(&statisticsManager.scratch.worldGrid.cursor, &readbackData->wgCursorValid, sizeof(WorldGridCursorCell));
        static_assert(sizeof(PickPixelResult) == offsetof(ReadbackStruct, pickWorldPos) + sizeof(float) * 3 - offsetof(ReadbackStruct, pickValid));
        std::memcpy(&statisticsManager.scratch.pick, &readbackData->pickValid, sizeof(PickPixelResult));
    }


    SanitizeViewFamily(viewFamily, pipelineManager, &renderArena.Get());
    PrepareRenderFamily(viewFamily);
    ctx.bufferSizes = ComputeSceneBufferSizes(viewFamily, readbackData, pipelineManager, frameResourceLimits);
    ctx.bCanRender = pipelineManager->IsCategoryReady(PipelineCategory::Critical) && !bRenderRequestsRecreate;
    ctx.bHasScene = ctx.bCanRender && viewFamily.instanceCount > 0;
    ctx.path = ResolveFrameRenderingPath(viewFamily);
    ctx.features = ComputeFrameFeatures(frameBuffer, viewFamily, ctx.path, ctx.bHasScene);
    ctx.needs = ComputeFrameNeeds(frameBuffer, viewFamily, ctx.path, ctx.features, ctx.bCanRender, ctx.bHasScene);
    return ctx;
}

void RenderThread::RecordFrameSetup(FrameContext& ctx, VkCommandBuffer cmd, VkCommandBuffer asyncCmd)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;

    //
    {
        ZoneScopedN("BindDescriptorBuffers");
        Core::Array<VkDescriptorBufferBindingInfoEXT, 3> bindings{
            resourceManager->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo(),
            resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo(),
            resourceManager->bindlessRDGRTDescriptorBuffer.GetBindingInfo()
        };
        Core::Array<uint32_t, 3> indices{0u, 1u, 2u};
        Core::Array<VkDeviceSize, 3> offsets{0, 0, 0};
        vkCmdBindDescriptorBuffersEXT(cmd, bindings.Size(), bindings.Data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineManager->GetGlobalPipelineLayout(), 0, bindings.Size(), indices.Data(), offsets.Data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineManager->GetGlobalPipelineLayout(), 0, bindings.Size(), indices.Data(), offsets.Data());
        vkCmdBindDescriptorBuffersEXT(asyncCmd, bindings.Size(), bindings.Data());
        vkCmdSetDescriptorBufferOffsetsEXT(asyncCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineManager->GetGlobalPipelineLayout(), 0, bindings.Size(), indices.Data(), offsets.Data());
    }
    //
    {
        ZoneScopedN("SetupUniforms");
        UploadFrameUniforms(viewFamily, renderExtent, ctx.frameBuffer.timeFrame.renderDeltaTime);
        UploadModelUniforms(viewFamily, ctx.bufferSizes);
        UploadTextUniforms(viewFamily, ctx.bufferSizes);
        UploadUIUniforms(viewFamily, ctx.bufferSizes);
        UploadSpriteUniforms(viewFamily);
    }
    //
    {
        ZoneScopedN("ImportBuffers");
        renderGraph->ImportBufferNoBarrier(GEOMETRY_VERTEX_POSITION_BUFFER, resourceManager->megaVertexPositionBuffer.handle, resourceManager->megaVertexPositionBuffer.address,
                                           {resourceManager->megaVertexPositionBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER, resourceManager->megaVertexAttributeBuffer.handle, resourceManager->megaVertexAttributeBuffer.address,
                                           {resourceManager->megaVertexAttributeBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(GEOMETRY_INDEX_BUFFER, resourceManager->megaIndexBuffer.handle, resourceManager->megaIndexBuffer.address,
                                           {resourceManager->megaIndexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("meshlet_vertex_buffer"_sid, resourceManager->megaMeshletVerticesBuffer.handle, resourceManager->megaMeshletVerticesBuffer.address,
                                           {resourceManager->megaMeshletVerticesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("meshlet_triangle_buffer"_sid, resourceManager->megaMeshletTrianglesBuffer.handle, resourceManager->megaMeshletTrianglesBuffer.address,
                                           {resourceManager->megaMeshletTrianglesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("meshlet_buffer"_sid, resourceManager->megaMeshletBuffer.handle, resourceManager->megaMeshletBuffer.address,
                                           {resourceManager->megaMeshletBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("primitive_buffer"_sid, resourceManager->primitiveBuffer.handle, resourceManager->primitiveBuffer.address,
                                           {resourceManager->primitiveBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(FONT_CURVE_BUFFER, resourceManager->megaFontCurveBuffer.handle, resourceManager->megaFontCurveBuffer.address,
                                           {resourceManager->megaFontCurveBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
#if WILL_EDITOR
        renderGraph->ImportBuffer("debug_readback_buffer"_sid,
                                  resourceManager->debugReadback.GetHandle(),
                                  resourceManager->debugReadback.GetAddress(),
                                  {resourceManager->debugReadback.GetSize(), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT},
                                  resourceManager->debugReadback.GetLastKnownState());
#endif
    }

    renderGraph->ImportTexture("dummy_black_rg32"_sid,
                               resourceManager->blackDummyRG32Image.handle,
                               resourceManager->blackDummyRG32ImageView.handle,
                               TextureInfo{GBUFFER_STABLE_ID_FORMAT, 1, 1, 1},
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                               VK_IMAGE_LAYOUT_GENERAL,
                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_IMAGE_LAYOUT_GENERAL);

    // Readback that will be copied into the FIF host memory at the end of the frame (to be read on frame N+3)
    renderGraph->CreateBuffer("readback_buffer"_sid, sizeof(ReadbackStruct), false);
    RenderPass& clearReadbackBuffer = renderGraph->AddPass("Clear Readback Buffer"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::Untagged);
    clearReadbackBuffer.WriteTransferBuffer("readback_buffer"_sid);
    clearReadbackBuffer.Execute([this](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, renderGraph->GetBufferHandle("readback_buffer"_sid), 0, VK_WHOLE_SIZE, 0);
    });

    ctx.targets = RenderTargets{
        .visibility = "visibility_target"_sid,
        .gbufferOne = "gbuffer_one"_sid,
        .gbufferTwo = "gbuffer_two"_sid,
        .shadowOriginOffset = "shadow_origin_offset"_sid,
        .shadows = "shadows_resolve_target"_sid,
        .intermediateOne = "intermediate_one"_sid,
        .intermediateTwo = "intermediate_two"_sid,
        .colorOutput = "shading_output"_sid,
        .depthStencil = "depth_target"_sid,
        .depthCopy = "depth_copy"_sid,
        .stableId = "stable_id"_sid,
    };
    const RenderTargets& targets = ctx.targets;

    renderGraph->CreateTexture(targets.visibility, TextureInfo{VISIBILITY_BUFFER_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_VISIBILITY_EMPTY, true);
    const bool bGeometry = ctx.bHasScene;
    auto declareGeometryTarget = [&](StringID name, const TextureInfo& info, std::optional<VkClearValue> clear) {
        if (bGeometry) { renderGraph->CreateVersionedTexture(name, info, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT, false, clear); }
        else if (renderGraph->ResourceHasVersion(name, 0)) { renderGraph->CreateVersionedTexture(name, info, 1, VersionSource::NoShiftReadOnly, true, VK_IMAGE_USAGE_SAMPLED_BIT); }
        else { renderGraph->CreateTexture(name, info, clear, true); }
    };
    declareGeometryTarget(targets.gbufferOne, TextureInfo{GBUFFER_TARGET_ONE, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY);
    renderGraph->CreateTexture(targets.gbufferTwo, TextureInfo{GBUFFER_TARGET_TWO, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.shadowOriginOffset, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.intermediateOne, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.intermediateTwo, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.colorOutput, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_DEPTH_FAR, true);
    declareGeometryTarget(targets.depthCopy, TextureInfo{VK_FORMAT_R32_SFLOAT, renderExtent[0], renderExtent[1], 1}, std::nullopt);
#if WILL_EDITOR
    renderGraph->CreateTexture(targets.stableId, TextureInfo{GBUFFER_STABLE_ID_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
#endif

    if (ctx.needs.bLitHistory) {
        renderGraph->CreateVersionedTexture(LIT_COLOR_HISTORY, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
        if (ctx.features.bGIGather && ctx.path == FrameRenderingPath::ReSTIR) {
            renderGraph->CreateTexture(RESTIR_DIFFUSE_RATIO, TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
            renderGraph->CreateVersionedTexture(GI_SCREEN_DIFFUSE, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
        }
    }
    // Without fog the lit history already holds the pre-overlay color
    if (ctx.needs.bPreOverlayColor) {
        ctx.targets.preOverlayColor = ctx.needs.bLitHistory && !ctx.features.bVolumetricFog ? LIT_COLOR_HISTORY : LIT_COLOR_PREOVERLAY;
        if (ctx.targets.preOverlayColor == LIT_COLOR_PREOVERLAY) {
            renderGraph->CreateTexture(LIT_COLOR_PREOVERLAY, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, std::nullopt, true);
        }
    }

    renderGraph->CreateVersionedBuffer("luminance_buffer"_sid, sizeof(float), 0, renderGraph->ResourceHasVersion("luminance_buffer"_sid, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh, 0,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    SetupSkyboxRendering(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0);

    SetupEmissiveTriLightPass(*renderGraph, pipelineManager, viewFamily, ctx.frameBuffer.restir.emissiveTriRangeMultiplier);
}

void RenderThread::RecordSceneServices(FrameContext& ctx)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;
    const RenderTargets& targets = ctx.targets;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;

    if (frameBuffer.debug.bEnableGPUDebug) {
        SetupGPUDebugBegin(*renderGraph, frameBuffer.debug.bLockGPUDebug);
    }

    if (viewFamily.instanceCount == 0) {
        return;
    }

    SetupGeometryPass(*renderGraph, pipelineManager, viewFamily, ctx.bufferSizes, frameBuffer.debug, renderExtent, targets, 0);

    SetupVisibilityBucketingPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameBuffer.debug.bucketDebugMode);

    SetupVisibilityShadingPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, renderArena.Get());

    SetupBucketDebugPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, frameBuffer.debug.bucketDebugMode);

    SetupTLASBuild(*renderGraph, context, pipelineManager, viewFamily, renderExtent, frameResourceLimits);

    const DDGICascades ddgiCascades = ComputeDDGICascades(frameBuffer.ddgi, viewFamily.mainView.currentViewData.cameraPos, viewFamily.localDDGIVolumes.Data(),
                                                          static_cast<uint32_t>(viewFamily.localDDGIVolumes.Size()), ddgiPreviousCascades, frameNumber, frameBuffer.debug.bFreezeGIField);

    if (ctx.needs.bWorldGrid) {
        SetupWorldGridBinningPass(*renderGraph, pipelineManager, viewFamily, 0, renderArena.Get(), ddgiCascades);
        if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bWorldGridDebug && !frameBuffer.debug.bLockGPUDebug) {
            SetupWorldGridDebug(*renderGraph, pipelineManager, 0, frameBuffer.debug.worldGridDebugLevel);
        }
    }

    if (ctx.features.ddgi != DDGIUsage::Off) {
        RecordDDGI(ctx, ddgiCascades);
    }
    else {
        ddgiPreviousCascades = DDGICascades{};
    }

    // Copy depth to R32_SFLOAT for all downstream compute passes.
    {
        auto& copyPass = renderGraph->AddPass("Depth Copy"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged);
        copyPass.ReadSampledImage(targets.depthStencil);
        copyPass.WriteStorageImage(targets.depthCopy);
        copyPass.Execute([depth = targets.depthStencil, depthCopy = targets.depthCopy,
                w = renderExtent[0], h = renderExtent[1], &pipelineManager = pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("depth_copy"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
                DepthCopyPushConstant pc{
                    .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                    .outputIndex = graph.GetStorageImageViewDescriptorIndex(depthCopy),
                    .extents = {w, h},
                };
                vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
            });
    }

    if (frameBuffer.debug.hizDebugMip >= 0) {
        SetupHiZDebug(*renderGraph, pipelineManager, renderExtent, frameBuffer.debug.hizDebugMip);
    }

    if (ctx.features.ddgi != DDGIUsage::Off && frameBuffer.debug.giDeconstructMode != 0) {
        SetupGIDeconstruct(*renderGraph, pipelineManager, renderExtent, targets, 0, frameBuffer.debug.giDeconstructMode);
    }

    if (ctx.features.bGTAO) {
        SetupGroundTruthAmbientOcclusion(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, frameNumber, 0);
    }

    // Outputs "shadows_resolve_target"
    SetupShadowsResolve(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0);
}

void RenderThread::RecordDDGI(FrameContext& ctx, const DDGICascades& ddgiCascades)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;

    const RadianceCacheFrame radianceCache = SetupRadianceCacheBegin(*renderGraph, pipelineManager, frameNumber, viewFamily.mainView.currentViewData.cameraPos, frameBuffer.debug.bFreezeGIField,
                                                                     frameBuffer.ddgi.radianceCacheShadeInterval);
    const bool bDDGIRecorded = SetupDDGIProbeUpdate(*renderGraph, pipelineManager, renderArena.Get(), frameBuffer.ddgi, ddgiCascades, ddgiPreviousCascades, viewFamily.skyboxIndex, viewFamily.iblIntensity,
                                                    frameNumber, frameBuffer.debug.bDDGIBounceOnly, radianceCache, static_cast<uint32_t>(viewFamily.reflectionProbes.Size()),
                                                    viewFamily.bReflectionProbeBruteForce, viewFamily.mainView.currentViewData.cameraPos, framerateScale);
    ddgiPreviousCascades = bDDGIRecorded ? ddgiCascades : DDGICascades{};
    const bool bRadianceCacheFeedback = frameBuffer.ddgi.bInfiniteBounce && !frameBuffer.debug.bDDGIBounceOnly;
    // Never below the configured cap: at low fps the cell responds slower instead of getting noisier.
    const auto radianceCacheAccumCap = glm::max(static_cast<uint32_t>(static_cast<float>(frameBuffer.ddgi.radianceCacheAccumCap) * framerateScale + 0.5f), frameBuffer.ddgi.radianceCacheAccumCap);
    SetupRadianceCacheShade(*renderGraph, pipelineManager, radianceCache, 0, bRadianceCacheFeedback, viewFamily.skyboxIndex, viewFamily.iblIntensity, frameBuffer.ddgi.maxRayRadiance,
                            frameBuffer.ddgi.bounceIntensity, radianceCacheAccumCap, static_cast<uint32_t>(viewFamily.reflectionProbes.Size()), viewFamily.bReflectionProbeBruteForce);
    if (GPU_STATS_ENABLED && radianceCache.bValid && renderGraph->HasBuffer("readback_buffer"_sid)) {
        RenderPass& wcStatsReadback = renderGraph->AddPass("Radiance Cache Stats Readback"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::RadianceCache);
        wcStatsReadback.ReadTransferBuffer(RADIANCE_CACHE_STATS);
        wcStatsReadback.ReadTransferBuffer(RADIANCE_CACHE_ACTIVE_COUNT);
        wcStatsReadback.WriteTransferBuffer("readback_buffer"_sid);
        wcStatsReadback.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const VkBuffer dst = graph.GetBufferHandle("readback_buffer"_sid);
            const VkBufferCopy statsCopy{0, offsetof(ReadbackStruct, wcOccupied), sizeof(RadianceCacheStats)};
            vkCmdCopyBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_STATS), dst, 1, &statsCopy);
            const VkBufferCopy shadedCopy{0, offsetof(ReadbackStruct, wcShaded), sizeof(uint32_t)};
            vkCmdCopyBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_ACTIVE_COUNT), dst, 1, &shadedCopy);
        });
    }
    if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bDDGIProbeDebug && !frameBuffer.debug.bLockGPUDebug) {
        SetupDDGIProbeDebug(*renderGraph, pipelineManager, ddgiCascades, frameBuffer.debug.ddgiProbeDebugExposure, frameBuffer.debug.ddgiProbeDebugCascade, frameBuffer.debug.bDDGIHideInactiveProbes,
                            frameBuffer.debug.ddgiProbeDebugMode);
    }
    if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bRadianceCacheDebug && !frameBuffer.debug.bLockGPUDebug) {
        SetupRadianceCacheDebug(*renderGraph, pipelineManager, radianceCache, frameBuffer.debug.radianceCacheDebugExposure, frameBuffer.debug.radianceCacheDebugBucket);
    }
}

void RenderThread::RecordLighting(FrameContext& ctx)
{
    if (ctx.path == FrameRenderingPath::GroundTruth) {
        RecordGroundTruth(ctx);
        return;
    }

    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;
    if (ctx.features.bGIGather) {
        const FinalGatherFrame giGather = SetupFinalGather(*renderGraph, pipelineManager, ctx.viewFamily, ctx.renderExtent, ctx.targets, 0, frameNumber, frameBuffer.ddgi.bFinalGatherDenoise,
                                                           frameBuffer.ddgi.bFinalGatherTemporal, frameBuffer.ddgi.gatherRaysPerPixel, false, frameBuffer.debug.bFreezeScreenFeedback,
                                                           frameBuffer.ddgi.bFinalGatherQuarterRes, frameBuffer.ddgi.bounceIntensity, frameBuffer.ddgi.maxRayRadiance);
        if (giGather.bValid) {
            ctx.giGatherMode = (frameBuffer.ddgi.bFinalGather && ctx.features.DDGIApplied()) ? 1u : 0u;
            SetupGIGatherDebug(*renderGraph, pipelineManager, ctx.renderExtent, frameBuffer.debug.giGatherDebugMode, frameBuffer.ddgi.bFinalGatherQuarterRes);
        }
    }

    switch (ctx.path) {
        case FrameRenderingPath::Default:
            RecordLightingDefault(ctx);
            break;
        case FrameRenderingPath::ReSTIR:
            RecordLightingReSTIR(ctx);
            break;
        case FrameRenderingPath::PathTracing:
            SetupRTShadowTest(*renderGraph, context, pipelineManager, ctx.viewFamily, ctx.renderExtent, ctx.targets, ctx.targets.colorOutput, 0);
            break;
        case FrameRenderingPath::GroundTruth:
            break;
    }

    if (ctx.features.sunShadow == SunShadowSource::RayTraced) {
        RecordSunShadows(ctx);
    }
}

void RenderThread::RecordGroundTruth(FrameContext& ctx)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    switch (viewFamily.groundTruthMode) {
        case Core::GroundTruthMode::DI:
        {
            if (viewFamily.bResetGroundTruth) { rtGroundTruthDIAccumCount = 0; }
            if (SetupRTGroundTruthDI(*renderGraph, pipelineManager, viewFamily, ctx.renderExtent, ctx.targets, 0, viewFamily.bResetGroundTruth, rtGroundTruthDIAccumCount, frameNumber)) {
                rtGroundTruthDIAccumCount += 1;
            }
            break;
        }
        case Core::GroundTruthMode::GI:
        {
            if (viewFamily.bResetGroundTruth) { rtGroundTruthGIAccumCount = 0; }
            if (SetupRTGroundTruthGI(*renderGraph, pipelineManager, viewFamily, ctx.renderExtent, ctx.targets, 0, viewFamily.bResetGroundTruth, rtGroundTruthGIAccumCount, frameNumber)) {
                rtGroundTruthGIAccumCount += 1;
            }
            break;
        }
        case Core::GroundTruthMode::Full:
        {
            if (viewFamily.bResetGroundTruth) { rtGroundTruthFullAccumCount = 0; }
            const uint32_t gtSpp = glm::max(1u, viewFamily.groundTruthSpp);
            if (SetupRTGroundTruthFull(*renderGraph, pipelineManager, viewFamily, ctx.renderExtent, ctx.targets, 0, viewFamily.bResetGroundTruth, rtGroundTruthFullAccumCount, frameNumber, gtSpp)) {
                rtGroundTruthFullAccumCount += gtSpp;
            }
            break;
        }
        case Core::GroundTruthMode::None:
            break;
    }
}

void RenderThread::RecordLightingDefault(FrameContext& ctx)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;
    const RenderTargets& targets = ctx.targets;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;

    if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bClusterGridDebug && !frameBuffer.debug.bLockGPUDebug) {
        constexpr float kDebugClusterZFar = 500.0f;
        SetupClusterGridDebug(*renderGraph, pipelineManager, 0, viewFamily.mainView.currentViewData.nearPlane, kDebugClusterZFar);
    }
    // No ReSTIR BRDF ray to piggyback on here, so reflections trace their own.
    if (frameBuffer.reflection.bScreenSpaceTrace) {
        SetupSSRTracePass(*renderGraph, pipelineManager, renderExtent, targets, 0, frameNumber, 0u, frameBuffer.reflection);
    }
    else {
        SetupReflectionTracePass(*renderGraph, pipelineManager, renderExtent, targets, 0, frameNumber, frameBuffer.reflection);
    }
    SetupReflectionShadePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, 0u, frameBuffer.reflection, ctx.features.DDGIApplied(), false, frameBuffer.debug.bFreezeScreenFeedback);
    SetupVisibilityLightingResolvePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, ctx.features.DDGIApplied(), ctx.giGatherMode, frameBuffer.reflection);
}

void RenderThread::RecordLightingReSTIR(FrameContext& ctx)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;
    const RenderTargets& targets = ctx.targets;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;
    const bool bDDGIApply = ctx.features.DDGIApplied();
    const uint32_t giGatherMode = ctx.giGatherMode;

    const Core::ReSTIRParams& restir = frameBuffer.restir;
    Core::RELAXParams relax = restir.relax;
    Core::ReBLURParams reblur = restir.reblur;
    relax.framerateScale = framerateScale;
    reblur.framerateScale = framerateScale;

    const uint32_t restirCheckerboardField = restir.bCheckerboard ? ((static_cast<uint32_t>(frameNumber) & 1u) ? 1u : 2u) : 0u;
    ctx.restirCheckerboardField = restirCheckerboardField;
    const bool bRestirFullRateResolve = restirCheckerboardField != 0u && restir.bCheckerboardFullRateResolve;
    const bool bRestirDenoised = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX || restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ReBLUR;
    const uint32_t restirCheckerboardPacked = (!bRestirFullRateResolve && bRestirDenoised) ? 1u : 0u;
    const float renderFps = frameBuffer.timeFrame.renderFps;
    const float restirCheckerboardResolveSpeed = ComputeCheckerboardResolveAccumSpeed(viewFamily.aaConfig.mode, frameNumber, renderFps, viewFamily.resolutionScale);
    const uint32_t denoiserCheckerboardField = bRestirFullRateResolve ? 0u : restirCheckerboardField;
    const float denoiserCheckerboardResolveSpeed = bRestirFullRateResolve ? 0.0f : restirCheckerboardResolveSpeed;

    const bool bResetReSTIRHistory = ((previousRestirCheckerboardField == 0u) != (restirCheckerboardField == 0u)) || (previousRestirFullRateResolve != bRestirFullRateResolve);
    previousRestirCheckerboardField = restirCheckerboardField;
    previousRestirFullRateResolve = bRestirFullRateResolve;
    const bool bScreenSpaceTrace = frameBuffer.reflection.bScreenSpaceTrace;
    SetupReSTIRPasses(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, renderArena.Get(), frameNumber, restir, restirCheckerboardField, frameBuffer.reflection, bResetReSTIRHistory,
                      bScreenSpaceTrace, preExposure);
    if (bScreenSpaceTrace) {
        SetupSSRTracePass(*renderGraph, pipelineManager, renderExtent, targets, 0, frameNumber, restirCheckerboardField, frameBuffer.reflection);
    }
    const bool bMergedReflections = frameBuffer.reflection.bMergedDenoise;
    const bool bReflectionCheckerboardPacked = bMergedReflections && restirCheckerboardPacked != 0u;
    SetupReflectionShadePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, restirCheckerboardField, frameBuffer.reflection, bDDGIApply, bReflectionCheckerboardPacked,
                             frameBuffer.debug.bFreezeScreenFeedback);
    SetupReSTIRLightingResolvePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, restirCheckerboardField, restirCheckerboardPacked, bRestirFullRateResolve ? 1u : 0u,
                                   frameBuffer.reflection);

    const uint32_t remodulateOutputMode = static_cast<uint32_t>(restir.remodulateOutput);

    if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX) {
        SetupRELAXDenoiser(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, relax, frameNumber, remodulateOutputMode, viewFamily.iblIntensity, denoiserCheckerboardField,
                           denoiserCheckerboardResolveSpeed, bDDGIApply, frameBuffer.reflection, giGatherMode, preExposure / prevPreExposure);
    }
    else if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ReBLUR) {
        SetupReBLURDenoiser(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, reblur, frameNumber, remodulateOutputMode, viewFamily.iblIntensity, denoiserCheckerboardField,
                            denoiserCheckerboardResolveSpeed, bDDGIApply, frameBuffer.reflection, giGatherMode, preExposure / prevPreExposure);
    }
    else if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRD || restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRDReBLUR) {
        const NrdBackend nrdBackend = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRDReBLUR ? NrdBackend::Reblur : NrdBackend::Relax;
        // Declaration order defines the RDG read/write sequence: prep writes -> dispatch -> writeback
        if (nrdDenoiser->Prepare(*renderGraph, viewFamily, renderExtent, nrdBackend, relax, reblur, frameNumber, ctx.frameIndex, renderFps)) {
            SetupNRDPrepPasses(*renderGraph, pipelineManager, renderExtent, targets, nrdBackend, reblur, preExposure);
            nrdDenoiser->AddDispatchPass(*renderGraph, resourceManager, pipelineManager, ctx.frameIndex);
            SetupNRDOutputPass(*renderGraph, pipelineManager, renderExtent, targets, nrdBackend, preExposure);
        }
        SetupReSTIRRemodulatePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, remodulateOutputMode, viewFamily.iblIntensity, frameNumber, bDDGIApply, frameBuffer.reflection,
                                  giGatherMode);
    }
    else {
        SetupReSTIRRemodulatePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, remodulateOutputMode, viewFamily.iblIntensity, frameNumber, bDDGIApply, frameBuffer.reflection,
                                  giGatherMode);
    }
}

void RenderThread::RecordSunShadows(FrameContext& ctx)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;
    const uint32_t sunShadowPixelScale = viewFamily.sigmaParams.bHalfRes ? 2u : 1u;
    const Core::Array<uint32_t, 2> sunShadowExtent = viewFamily.sigmaParams.bHalfRes ? Core::Array<uint32_t, 2>{renderExtent[0] / 2, renderExtent[1] / 2} : renderExtent;
    SetupRTSunShadow(*renderGraph, pipelineManager, viewFamily, sunShadowExtent, renderExtent, ctx.targets, 0, frameNumber, sunShadowPixelScale);
    SetupSigmaShadowDenoise(*renderGraph, pipelineManager, viewFamily, sunShadowExtent, ctx.targets, 0, frameNumber);
    SetupSigmaShadowTemporal(*renderGraph, pipelineManager, viewFamily, sunShadowExtent, ctx.targets, 0);
    SetupDirectionalLightingPass(*renderGraph, pipelineManager, viewFamily, renderExtent, sunShadowExtent, ctx.targets, 0, sunShadowPixelScale);
}

void RenderThread::RecordPostLighting(FrameContext& ctx)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;
    const RenderTargets& targets = ctx.targets;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;

    if (ctx.needs.bLitHistory) {
        AddColorCopyPass(*renderGraph, pipelineManager, "Lit Color Snapshot"_sid, targets.colorOutput, LIT_COLOR_HISTORY, renderExtent);
    }

    const bool bPreOverlayCopy = ctx.targets.preOverlayColor == LIT_COLOR_PREOVERLAY;
    if (ctx.features.bVolumetricFog) {
        SetupVolumetricFog(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, bPreOverlayCopy, ctx.features.DDGIApplied(),
                           frameBuffer.debug.fogDebugMode, frameBuffer.debug.fogDebugMode != lastFogDebugMode);
        lastFogDebugMode = frameBuffer.debug.fogDebugMode;
    }
    else if (bPreOverlayCopy) {
        AddColorCopyPass(*renderGraph, pipelineManager, "Pre-Overlay Color Copy"_sid, targets.colorOutput, LIT_COLOR_PREOVERLAY, renderExtent);
    }

#if WILL_EDITOR
    debugCursorReadback.litTexture = targets.colorOutput;
#endif

    SetupTextForwardPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);
    SetupSpritesPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);

#if WILL_EDITOR
    if (frameBuffer.selectedStableId != 0) {
        SetupSelectionOutlinePass(*renderGraph, pipelineManager, renderExtent, targets, frameBuffer.selectedStableId);
    }
#endif

    SetupDebugRender(*renderGraph, viewFamily, renderExtent, targets.depthStencil, targets.colorOutput, frameResourceLimits);

    SetupProbePreviewSpheres(*renderGraph, pipelineManager, renderExtent, targets.depthStencil, targets.colorOutput, viewFamily);

    if (frameBuffer.debug.bEnableGPUDebug) {
        SetupGPUDebugDraw(*renderGraph, pipelineManager, renderExtent, targets.depthStencil, targets.colorOutput, frameBuffer.debug.bLockGPUDebug);
    }
}

void RenderThread::RecordPresentation(FrameContext& ctx)
{
    Core::ViewFamily& viewFamily = ctx.viewFamily;
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;
    RenderTargets& targets = ctx.targets;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;
    const Core::Array<uint32_t, 2> outputExtent = ctx.outputExtent;

    if (ctx.path != FrameRenderingPath::GroundTruth) {
        targets.colorOutput = PPDepthOfField(*renderGraph, pipelineManager, viewFamily.postProcessConfig, targets, renderExtent, frameNumber, targets.colorOutput);
    }

    switch (viewFamily.aaConfig.mode) {
        case Core::AntiAliasingMode::SMAA:
            targets.colorOutput = SetupSubpixelMorphologicalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);
            break;
        case Core::AntiAliasingMode::TAA:
            targets.colorOutput = SetupTemporalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, "taa_main"_sid);
            break;
        case Core::AntiAliasingMode::NaiveTAA:
            targets.colorOutput = SetupTemporalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, "taa_naive"_sid);
            break;
        case Core::AntiAliasingMode::DonutTAA:
            targets.colorOutput = SetupDonutTemporalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, outputExtent, targets);
            ctx.postAaExtent = outputExtent;
            break;
        case Core::AntiAliasingMode::FSR2:
            targets.colorOutput = SetupFsr2(*renderGraph, pipelineManager, viewFamily, renderExtent, outputExtent, targets, frameBuffer.reflection,
                                            frameBuffer.timeFrame.renderDeltaTime, framerateScale, frameNumber, preExposure, prevPreExposure);
            ctx.postAaExtent = outputExtent;
            break;
        case Core::AntiAliasingMode::SMAAT2X:
            targets.colorOutput = SetupSMAA_T2X(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);
            break;
        default: break;
    }

    if (frameBuffer.bCaptureProbeFace && screenCapture->CanProbeCapture()) {
        RecordProbeCapture(ctx);
    }

    const Core::Array<uint32_t, 2> postAaExtent = ctx.postAaExtent;
    targets.colorOutput = SetupPostProcessing(*renderGraph, pipelineManager, viewFamily, postAaExtent, renderExtent, outputExtent, targets, frameBuffer.timeFrame.renderDeltaTime, frameNumber, preExposure);

    if (!viewFamily.screenFade.bDrawOverUI) {
        targets.colorOutput = PPScreenFade(*renderGraph, pipelineManager, viewFamily.screenFade, postAaExtent, targets.colorOutput);
    }

    SetupUIRender(*renderGraph, pipelineManager, viewFamily, postAaExtent, targets.colorOutput);

    if (viewFamily.screenFade.bDrawOverUI) {
        targets.colorOutput = PPScreenFade(*renderGraph, pipelineManager, viewFamily.screenFade, postAaExtent, targets.colorOutput);
    }
}

void RenderThread::RecordProbeCapture(FrameContext& ctx)
{
    const Core::Array<uint32_t, 2> postAaExtent = ctx.postAaExtent;
    const uint32_t minSquare = std::min(postAaExtent[0], postAaExtent[1]) & ~1u;
    uint32_t captureSquare = ctx.frameBuffer.probeCaptureCropSize > 0 ? std::min(ctx.frameBuffer.probeCaptureCropSize, minSquare) : minSquare;
    if (captureSquare < 2) {
        return;
    }

    screenCapture->PrepareProbeCaptureResources(captureSquare);
    renderGraph->CreateTexture("probe_capture_intermediate"_sid, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, captureSquare, captureSquare, 1}, CLEAR_COLOR_EMPTY, true);

    auto& probeCaptureBlitPass = renderGraph->AddPass("Probe Capture Blit"_sid, VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged);
    probeCaptureBlitPass.ReadBlitImage(ctx.targets.colorOutput);
    probeCaptureBlitPass.WriteBlitImage("probe_capture_intermediate"_sid);
    probeCaptureBlitPass.Execute([this, colorOutput = ctx.targets.colorOutput, s = captureSquare, w = postAaExtent[0], h = postAaExtent[1]](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.srcOffsets[0] = {static_cast<int32_t>((w - s) / 2), static_cast<int32_t>((h - s) / 2), 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>((w + s) / 2), static_cast<int32_t>((h + s) / 2), 1};
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(s), static_cast<int32_t>(s), 1};

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage = renderGraph->GetImageHandle(colorOutput);
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = renderGraph->GetImageHandle("probe_capture_intermediate"_sid);
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_NEAREST;
        vkCmdBlitImage2(_cmd, &blitInfo);
    });

    auto& probeCaptureCopyPass = renderGraph->AddPass("Probe Capture Copy"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
    probeCaptureCopyPass.ReadCopyImage("probe_capture_intermediate"_sid);
    probeCaptureCopyPass.Execute([this, s = captureSquare](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
        VkBufferImageCopy2 copyRegion{};
        copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent = {s, s, 1};

        VkCopyImageToBufferInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copyInfo.srcImage = renderGraph->GetImageHandle("probe_capture_intermediate"_sid);
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstBuffer = screenCapture->probeCaptureReadbackBuffer.handle;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &copyRegion;
        vkCmdCopyImageToBuffer2(_cmd, &copyInfo);
    });

    screenCapture->probeCapturePreExposure = preExposure;
    screenCapture->probeCapturePendingSlot = ctx.frameIndex;
    screenCapture->StartProbeCapture();
}

#if WILL_EDITOR
void RenderThread::RecordDiagnostics(FrameContext& ctx)
{
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;
    const RenderTargets& targets = ctx.targets;
    const Core::Array<uint32_t, 2> renderExtent = ctx.renderExtent;
    const Core::Array<uint32_t, 2> outputExtent = ctx.outputExtent;

    Core::Array<uint32_t, 2> cursorPixel{};
    if (frameBuffer.currentMousePosition[0] > 0 && frameBuffer.currentMousePosition[0] < outputExtent[0] &&
        frameBuffer.currentMousePosition[1] > 0 && frameBuffer.currentMousePosition[1] < outputExtent[1] &&
        DisplayUvToRenderPixel((static_cast<float>(frameBuffer.currentMousePosition[0]) + 0.5f) / static_cast<float>(outputExtent[0]),
                               (static_cast<float>(frameBuffer.currentMousePosition[1]) + 0.5f) / static_cast<float>(outputExtent[1]),
                               ctx.displayPanini, ctx.displayAspect, renderExtent, cursorPixel)) {
        debugCursorReadback.pixel[0] = cursorPixel[0];
        debugCursorReadback.pixel[1] = cursorPixel[1];
        if (GPU_STATS_ENABLED) {
            if (frameBuffer.debug.bWorldGridCursorCell && frameBuffer.restir.lightProposal == Core::ReSTIRParams::LightProposal::WorldGridBin) {
                SetupDebugWorldGridCursorCellPass(*renderGraph, pipelineManager, 0, targets.depthCopy, renderExtent, {debugCursorReadback.pixel[0], debugCursorReadback.pixel[1]});
            }
            if (frameBuffer.debug.bReGIRCursorCell && frameBuffer.restir.lightProposal == Core::ReSTIRParams::LightProposal::ReGIR) {
                SetupDebugReGIRCursorCellPass(*renderGraph, pipelineManager, 0, targets.depthCopy, renderExtent, {debugCursorReadback.pixel[0], debugCursorReadback.pixel[1]});
            }
        }
    }
    else {
        debugCursorReadback.litTexture = StringID{};
    }

    if (frameBuffer.debug.pickRequestId != 0u) {
        Core::Array<uint32_t, 2> pickPixel = renderExtent;
        DisplayUvToRenderPixel(std::clamp(frameBuffer.debug.pickU, 0.0f, 1.0f), 1.0f - std::clamp(frameBuffer.debug.pickV, 0.0f, 1.0f), ctx.displayPanini, ctx.displayAspect, renderExtent, pickPixel);
        SetupDebugPickPixelPass(*renderGraph, pipelineManager, 0, targets.visibility, targets.depthCopy, renderExtent, pickPixel, frameBuffer.debug.pickRequestId);
    }
    resourceManager->debugReadback.ScheduleCopies(*renderGraph, "debug_readback_buffer"_sid);

    if (!ctx.viewFamily.debugResourceName.IsEmpty()) {
        RecordDebugVisualize(ctx);
    }
}

void RenderThread::RecordDebugVisualize(FrameContext& ctx)
{
    const Core::ViewFamily& viewFamily = ctx.viewFamily;
    StringID debugTargetName = StringID(viewFamily.debugResourceName.c_str(), viewFamily.debugResourceName.Size());

    bool bDebugBuffersReady = renderGraph->HasBuffer(SCENE_DATA_BUFFER);

    bool bDebugReservoirReady = true;
    switch (viewFamily.debugTransformationType) {
        case DebugTransformationType::ReservoirLightIdx:
        case DebugTransformationType::ReservoirGenerateW:
            bDebugReservoirReady = renderGraph->HasBuffer("restir_reservoir_base"_sid);
            break;
        case DebugTransformationType::ReservoirTemporalLightIdx:
        case DebugTransformationType::ReservoirTemporalW:
            bDebugReservoirReady = renderGraph->HasBuffer("restir_reservoir_temporal"_sid);
            break;
        case DebugTransformationType::ReservoirSpatialLightIdx:
        case DebugTransformationType::ReservoirSpatialW:
            bDebugReservoirReady = renderGraph->HasBuffer("restir_reservoir_spatial"_sid);
            break;
        case DebugTransformationType::ReservoirHistoryLightIdx:
        case DebugTransformationType::ReservoirHistoryW:
            bDebugReservoirReady = renderGraph->ResourceHasVersion("restir_reservoir_history"_sid, 1);
            break;
        default:
            break;
    }

    if (!bDebugBuffersReady || !bDebugReservoirReady || !renderGraph->HasTexture(debugTargetName) || !renderGraph->HasTexture(ctx.targets.depthCopy)) {
        return;
    }

    auto& debugVisPass = renderGraph->AddPass("Debug Visualize"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Debug);
    debugVisPass.ReadSampledImage(debugTargetName);
    debugVisPass.ReadSampledImage(ctx.targets.depthCopy);
    const StringID debugVisBuffers[] = {
        SCENE_DATA_BUFFER,
        GEOMETRY_VERTEX_POSITION_BUFFER,
        GEOMETRY_VERTEX_ATTRIBUTE_BUFFER,
        GEOMETRY_MESHLET_VERTEX_BUFFER,
        GEOMETRY_MESHLET_TRIANGLE_BUFFER,
        GEOMETRY_MESHLET_BUFFER,
        GEOMETRY_PRIMITIVE_BUFFER,
        GEOMETRY_INSTANCE_BUFFER,
        GEOMETRY_MODEL_BUFFER,
        GEOMETRY_MATERIAL_BUFFER,
        "restir_reservoir_base"_sid,
        "restir_reservoir_temporal"_sid,
        "restir_reservoir_spatial"_sid,
        renderGraph->ResourceVersionID("restir_reservoir_history"_sid, 1),
        REFLECTION_PROBE_BUFFER,
        "world_grid_probe_grid"_sid,
        LIGHT_DATA_BUFFER,
        "regir_hash_entries"_sid,
        "regir_cell_data"_sid,
    };
    for (const StringID bufferId : debugVisBuffers) {
        if (renderGraph->HasBuffer(bufferId)) {
            debugVisPass.ReadBuffer(bufferId);
        }
    }
    debugVisPass.WriteStorageImage(ctx.targets.colorOutput);
    debugVisPass.Execute([this, &ctx, debugTargetName, colorOutput = ctx.targets.colorOutput](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
        const Core::ViewFamily& viewFamily = ctx.viewFamily;
        const ResourceDimensions& dims = renderGraph->GetImageDimensions(debugTargetName);
        if (dims.Is3D()) { return; }
        VkImageAspectFlags aspect = renderGraph->GetImageAspect(debugTargetName);

        VkImageAspectFlags viewAspect = aspect;
        if (viewFamily.debugViewAspect == Core::DebugViewAspect::Depth) {
            viewAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else if (viewFamily.debugViewAspect == Core::DebugViewAspect::Stencil) {
            viewAspect = VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        ImageChannelType storageType = GetImageChannelType(dims.format, viewAspect);
        uint32_t textureArrayIndex{3};
        switch (storageType) {
            case ImageChannelType::Float4:
                textureArrayIndex = 0;
                break;
            case ImageChannelType::Float2:
                textureArrayIndex = 1;
                break;
            case ImageChannelType::Float:
                textureArrayIndex = 2;
                break;
            case ImageChannelType::UInt4:
                textureArrayIndex = 3;
                break;
            case ImageChannelType::UInt2:
                textureArrayIndex = 4;
                break;
            case ImageChannelType::UInt:
                textureArrayIndex = 5;
                break;
        }

        uint32_t textureIndexInArray = renderGraph->GetSampledImageViewDescriptorIndex(debugTargetName);
        if (viewFamily.debugViewAspect == Core::DebugViewAspect::Depth) {
            textureIndexInArray = renderGraph->GetDepthOnlySampledImageViewDescriptorIndex(debugTargetName);
        }
        else if (viewFamily.debugViewAspect == Core::DebugViewAspect::Stencil) {
            // uint storage descriptor array
            textureArrayIndex = 7;
            textureIndexInArray = renderGraph->GetStencilOnlyStorageImageViewDescriptorIndex(debugTargetName);
        }

        uint32_t outputIndexIndex = renderGraph->GetStorageImageViewDescriptorIndex(colorOutput);

        const uint32_t checkerboardField = ctx.restirCheckerboardField;
        const uint32_t historyCheckerboardField = checkerboardField == 0u ? 0u : (3u - checkerboardField);

        DebugVisualizePushConstant pc{
            .sceneData = renderGraph->TryGetBufferAddress(SCENE_DATA_BUFFER),
            .vertexPosBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
            .vertexAttrBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .meshletVerticesBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
            .meshletTrianglesBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
            .meshletBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MESHLET_BUFFER),
            .primitiveBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .instanceBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .modelBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .reservoirBuffer = renderGraph->TryGetBufferAddress("restir_reservoir_base"_sid),
            .reservoirTemporalBuffer = renderGraph->TryGetBufferAddress("restir_reservoir_temporal"_sid),
            .reservoirSpatialBuffer = renderGraph->TryGetBufferAddress("restir_reservoir_spatial"_sid),
            .reservoirHistoryBuffer = renderGraph->TryGetBufferAddress(renderGraph->ResourceVersionID("restir_reservoir_history"_sid, 1)),
            .srcExtent = {ctx.renderExtent[0], ctx.renderExtent[1]},
            .dstExtent = {ctx.postAaExtent[0], ctx.postAaExtent[1]},
            .nearPlane = viewFamily.mainView.currentViewData.nearPlane,
            .textureArrayIndex = textureArrayIndex,
            .textureIndexInArray = textureIndexInArray,
            .valueTransformationType = static_cast<uint32_t>(viewFamily.debugTransformationType),
            .outputImageIndex = outputIndexIndex,
            .depthTextureIndex = renderGraph->GetSampledImageViewDescriptorIndex(ctx.targets.depthCopy),
            .checkerboardField = checkerboardField,
            .historyCheckerboardField = historyCheckerboardField,
            .reflectionProbes = viewFamily.reflectionProbes.Size() > 0u ? renderGraph->TryGetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size()),
            .dofPackedRadii = glm::packHalf2x16(glm::vec2(viewFamily.postProcessConfig.dofNearRadiusPx, viewFamily.postProcessConfig.dofFarRadiusPx)),
            .worldGridProbeGrid = viewFamily.bReflectionProbeBruteForce ? 0 : renderGraph->TryGetBufferAddress("world_grid_probe_grid"_sid),
            .lightData = renderGraph->TryGetBufferAddress(LIGHT_DATA_BUFFER),
            .regirHashEntries = renderGraph->TryGetBufferAddress("regir_hash_entries"_sid),
            .regirCellData = renderGraph->TryGetBufferAddress("regir_cell_data"_sid),
        };
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_visualize"_sid);
        vkCmdBindPipeline(_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(_cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (ctx.postAaExtent[0] + 15) / 16;
        uint32_t yDispatch = (ctx.postAaExtent[1] + 15) / 16;
        vkCmdDispatch(_cmd, xDispatch, yDispatch, 1);
    });
}
#endif

void RenderThread::RecordFrameExport(FrameContext& ctx)
{
    Core::FrameBuffer& frameBuffer = ctx.frameBuffer;

    // Leaves the color output in TRANSFER_SRC for RecordPresent, which blits it after the swapchain acquire.
    presentSourceTexture = ctx.targets.colorOutput;
    auto& exportPass = renderGraph->AddPass("Export Color Output"_sid, VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged);
    exportPass.ReadBlitImage(ctx.targets.colorOutput);
    exportPass.Execute([](VkCommandBuffer, VulkanContext*, RenderGraph&) {});

    if (frameBuffer.bTakeScreenshot) {
        RecordScreenshot(ctx);
    }

#if WILL_EDITOR
    const Core::Array<uint32_t, 2> outputExtent = ctx.outputExtent;
    if (frameBuffer.currentMousePosition[0] > 0 && frameBuffer.currentMousePosition[0] < outputExtent[0] &&
        frameBuffer.currentMousePosition[1] > 0 && frameBuffer.currentMousePosition[1] < outputExtent[1]) {
        Core::Array<uint32_t, 2> mousePixel{};
        const bool bMouseOnSource = DisplayUvToRenderPixel((static_cast<float>(frameBuffer.currentMousePosition[0]) + 0.5f) / static_cast<float>(outputExtent[0]),
                                                           (static_cast<float>(frameBuffer.currentMousePosition[1]) + 0.5f) / static_cast<float>(outputExtent[1]),
                                                           ctx.displayPanini, ctx.displayAspect, ctx.renderExtent, mousePixel);
        RenderPass& copyStableId = renderGraph->AddPass("Copy Stable ID"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        copyStableId.ReadCopyImage("stable_id"_sid);
        copyStableId.WriteTransferBuffer("readback_buffer"_sid);
        copyStableId.Execute([this, bMouseOnSource, mouseX = mousePixel[0], mouseY = mousePixel[1]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            if (!bMouseOnSource) {
                vkCmdFillBuffer(cmd, renderGraph->GetBufferHandle("readback_buffer"_sid), offsetof(ReadbackStruct, selectedStableId), sizeof(uint64_t), 0u);
                return;
            }
            VkBufferImageCopy region{};
            region.bufferOffset = offsetof(ReadbackStruct, selectedStableId);
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {static_cast<int32_t>(mouseX), static_cast<int32_t>(mouseY), 0};
            region.imageExtent = {1, 1, 1};

            vkCmdCopyImageToBuffer(
                cmd,
                renderGraph->GetImageHandle("stable_id"_sid),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                renderGraph->GetBufferHandle("readback_buffer"_sid),
                1,
                &region
            );
        });
    }
#endif

    RenderPass& readbackMeshletCount = renderGraph->AddPass("[Critical] Readback Copy"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
    readbackMeshletCount.ReadTransferBuffer("readback_buffer"_sid);
    readbackMeshletCount.Execute([this](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkBufferCopy copy;
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = sizeof(ReadbackStruct);

        vkCmdCopyBuffer(
            cmd,
            renderGraph->GetBufferHandle("readback_buffer"_sid),
            renderGraph->GetReadback(),
            1,
            &copy
        );
    });
}

void RenderThread::RecordScreenshot(FrameContext& ctx)
{
    const Core::Array<uint32_t, 2> postAaExtent = ctx.postAaExtent;
    screenCapture->PrepareScreenshotResources(postAaExtent[0], postAaExtent[1]);
    const uint32_t screenshotSlot = screenCapture->AcquireScreenshotSlot();
    RenderScreenCapture::ScreenshotSlot& slot = screenCapture->screenshotSlots[screenshotSlot];
    renderGraph->CreateTexture("screenshot_intermediate"_sid, TextureInfo{VK_FORMAT_R8G8B8A8_SRGB, postAaExtent[0], postAaExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    auto& screenshotBlitPass = renderGraph->AddPass("Screenshot Blit"_sid, VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged);
    screenshotBlitPass.ReadBlitImage(ctx.targets.colorOutput);
    screenshotBlitPass.WriteBlitImage("screenshot_intermediate"_sid);
    screenshotBlitPass.Execute([this, colorOutput = ctx.targets.colorOutput, w = postAaExtent[0], h = postAaExtent[1]](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(w), static_cast<int32_t>(h), 1};
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(w), static_cast<int32_t>(h), 1};

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage = renderGraph->GetImageHandle(colorOutput);
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = renderGraph->GetImageHandle("screenshot_intermediate"_sid);
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_NEAREST;
        vkCmdBlitImage2(_cmd, &blitInfo);
    });

    auto& screenshotCopyPass = renderGraph->AddPass("Screenshot Copy"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
    screenshotCopyPass.ReadCopyImage("screenshot_intermediate"_sid);
    screenshotCopyPass.Execute([this, readback = slot.readbackBuffer.handle](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
        VkBufferImageCopy2 copyRegion{};
        copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent = {screenCapture->screenshotCaptureWidth, screenCapture->screenshotCaptureHeight, 1};

        VkCopyImageToBufferInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copyInfo.srcImage = renderGraph->GetImageHandle("screenshot_intermediate"_sid);
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstBuffer = readback;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &copyRegion;
        vkCmdCopyImageToBuffer2(_cmd, &copyInfo);
    });

    if (ctx.frameBuffer.screenshotPath.IsEmpty()) {
        Core::Path screenshotDir = Platform::GetUserDataPath() / "screenshots";
        Platform::CreateDirectories(screenshotDir.c_str());

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
        const auto filename = Core::InlineString<>::Format("%s_%llu.png", timestamp, static_cast<unsigned long long>(frameNumber));
        slot.savePath = screenshotDir / filename.c_str();
    }
    else {
        slot.savePath = Core::Path(ctx.frameBuffer.screenshotPath.c_str());
        Platform::CreateDirectories(slot.savePath.Parent().c_str());
    }
    slot.pendingFrameIndex = ctx.frameIndex;
}
} // Render
