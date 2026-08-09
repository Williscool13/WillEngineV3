//
// Created by William on 2026-08-08.
//

#include "render/passes/occlusion_passes.h"

#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
static uint32_t PreviousPow2(uint32_t v)
{
    uint32_t p = 1;
    while (p * 2u <= v) { p *= 2u; }
    return p;
}

void SetupHiZPyramid(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets)
{
    const uint32_t mip0W = PreviousPow2(renderExtent[0] > 1u ? renderExtent[0] / 2u : 1u);
    const uint32_t mip0H = PreviousPow2(renderExtent[1] > 1u ? renderExtent[1] / 2u : 1u);
    uint32_t mipCount = 1;
    while ((mip0W >> mipCount) > 0u || (mip0H >> mipCount) > 0u) { mipCount++; }

    graph.CreateTexture(HIZ_PYRAMID, TextureInfo{VK_FORMAT_R32_SFLOAT, mip0W, mip0H, mipCount}, {std::nullopt}, true);

    constexpr uint32_t MIPS_PER_DISPATCH = 5;
    for (uint32_t chunkStart = 0; chunkStart < mipCount; chunkStart += MIPS_PER_DISPATCH) {
        const uint32_t chunkMips = mipCount - chunkStart < MIPS_PER_DISPATCH ? mipCount - chunkStart : MIPS_PER_DISPATCH;
        const uint32_t dstW = mip0W >> chunkStart > 0u ? mip0W >> chunkStart : 1u;
        const uint32_t dstH = mip0H >> chunkStart > 0u ? mip0H >> chunkStart : 1u;
        const uint32_t srcW = chunkStart == 0 ? renderExtent[0] : dstW * 2u;
        const uint32_t srcH = chunkStart == 0 ? renderExtent[1] : dstH * 2u;

        Core::InlineString<48> passName = Core::InlineString<48>::Format("[Geometry P2] HiZ Build %u-%u", chunkStart, chunkStart + chunkMips - 1);
        RenderPass& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::GeometryPhase2);
        if (chunkStart == 0) {
            pass.ReadSampledImage(targets.depthStencil);
            pass.WriteStorageImage(HIZ_PYRAMID);
        }
        else {
            pass.ReadWriteImage(HIZ_PYRAMID);
        }
        pass.Execute([pipelineManager, chunkStart, chunkMips, srcW, srcH, dstW, dstH, depthStencil = targets.depthStencil](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("hiz_build"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            HiZBuildPushConstant pc{
                .srcExtent = {srcW, srcH},
                .dstExtent = {dstW, dstH},
                .srcIndex = chunkStart == 0 ? graph.GetDepthOnlySampledImageViewDescriptorIndex(depthStencil) : graph.GetStorageImageViewDescriptorIndex(HIZ_PYRAMID, chunkStart - 1),
                .dstMipCount = chunkMips,
                .bSampledSrc = chunkStart == 0 ? 1u : 0u,
            };
            for (uint32_t i = 0; i < chunkMips; i++) {
                pc.dstIndex[i] = graph.GetStorageImageViewDescriptorIndex(HIZ_PYRAMID, chunkStart + i);
            }
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (dstW + 15u) / 16u, (dstH + 15u) / 16u, 1);
        });
    }
}

void SetupHiZDebug(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, int32_t mip)
{
    if (mip < 0 || !graph.HasTexture(HIZ_PYRAMID)) {
        return;
    }

    graph.CreateTexture(HIZ_DEBUG_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass(SID("HiZ Debug"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadSampledImage(HIZ_PYRAMID);
    pass.WriteStorageImage(HIZ_DEBUG_TARGET);
    pass.Execute([pipelineManager, renderExtent, mip](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("hiz_debug"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
        const ResourceDimensions& dims = graph.GetImageDimensions(HIZ_PYRAMID);
        const uint32_t clampedMip = static_cast<uint32_t>(mip) < dims.levels ? static_cast<uint32_t>(mip) : dims.levels - 1u;
        const uint32_t mipW = dims.width >> clampedMip;
        const uint32_t mipH = dims.height >> clampedMip;
        HiZDebugPushConstant pc{
            .hizIndex = graph.GetSampledImageViewDescriptorIndex(HIZ_PYRAMID),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(HIZ_DEBUG_TARGET),
            .outExtent = {renderExtent[0], renderExtent[1]},
            .mipExtent = {mipW > 0u ? mipW : 1u, mipH > 0u ? mipH : 1u},
            .mip = clampedMip,
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });
}
} // Render
