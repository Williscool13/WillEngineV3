//
// Created by William on 2026-06-03.
//

#include "render/passes/scene_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"

namespace Render
{
void SetupSkyboxRendering(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const RenderTargets& targets,
                          uint32_t sceneIndex)
{
    RenderPass& skyboxPass = graph.AddPass(
        SID("Skybox"), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, RenderCategory::Scene);
    skyboxPass.ReadBuffer(SCENE_DATA_BUFFER);
    skyboxPass.WriteColorAttachment(targets.colorOutput);
    skyboxPass.ReadWriteDepthAttachment(targets.depthStencil);
    skyboxPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            outputColor = targets.colorOutput, depthStencil = targets.depthStencil, skyboxIndex = viewFamily.skyboxIndex, skyboxLOD = viewFamily.skyboxLOD](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkViewport viewport = VkHelpers::GenerateViewport(width, height);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            auto colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(outputColor), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            auto depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            Core::Array<VkRenderingAttachmentInfo, 1> colorAttachments{colorAttachment};
            VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments.Data(), 1, &depthAttachment, nullptr);
            vkCmdBeginRendering(cmd, &renderInfo);

            EnvironmentSkyboxPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .sceneDataIndex = sceneIndex,
                .cubemapIndex = skyboxIndex,
                .skyboxLOD = skyboxLOD,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("environment_skybox"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

            vkCmdDraw(cmd, 3, 1, 0, 0);

            vkCmdEndRendering(cmd);
        });
}

void SetupTextForwardPass(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const RenderTargets& targets)
{
    if (viewFamily.worldGlyphQuads.IsEmpty()) { return; }
    if (!graph.HasBuffer(TEXT_GLYPH_QUAD_BUFFER) || !graph.HasBuffer(TEXT_INSTANCE_BUFFER) || !graph.HasBuffer(TEXT_MATERIAL_BUFFER)) { return; }

    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("text_default"));
    if (!pipelineEntry) { return; }

    RenderPass& textPass = graph.AddPass(SID("Text Forward"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, Render::RenderCategory::Scene);
    textPass.ReadBuffer(SCENE_DATA_BUFFER);
    textPass.ReadBuffer(TEXT_GLYPH_QUAD_BUFFER);
    textPass.ReadBuffer(TEXT_INSTANCE_BUFFER);
    textPass.ReadBuffer(TEXT_MATERIAL_BUFFER);
    textPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    //textPass.ReadDepthAttachment(targets.depthStencil);
    textPass.ReadWriteDepthAttachment(targets.depthStencil);
    textPass.WriteColorAttachment(targets.colorOutput);
    textPass.WriteColorAttachment(targets.stableId);
    textPass.Execute([&, width = renderExtent[0], height = renderExtent[1], pipelineEntry, colorOutput = targets.colorOutput, depthOutput = targets.depthStencil, stableId = targets.stableId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkImageView colorView = graph.GetImageViewHandle(colorOutput);
        VkImageView depthView = graph.GetImageViewHandle(depthOutput);
        VkImageView stableIdView = graph.GetImageViewHandle(stableId);
        VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(colorView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo stableIdAttachment = VkHelpers::RenderingAttachmentInfo(stableIdView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        // VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(depthView, nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(depthView, nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo colorAttachments[] = {colorAttachment, stableIdAttachment};
        VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 2, &depthAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);

        VkDeviceAddress sceneDataAddr = graph.GetBufferAddress(SCENE_DATA_BUFFER);
        VkDeviceAddress glyphQuadsAddr = graph.GetBufferAddress(TEXT_GLYPH_QUAD_BUFFER);
        VkDeviceAddress instAddr = graph.GetBufferAddress(TEXT_INSTANCE_BUFFER);
        VkDeviceAddress modelAddr = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER);
        VkDeviceAddress matAddr = graph.GetBufferAddress(TEXT_MATERIAL_BUFFER);

        for (const Core::TextDrawCall& dc : viewFamily.textDrawCalls) {
            uint32_t groupCount = (dc.quadCount + 15) / 16;
            TextRenderPushConstant pc{
                .sceneData = sceneDataAddr,
                .worldGlyphQuads = glyphQuadsAddr,
                .textInstanceData = instAddr,
                .modelBuffer = modelAddr,
                .textMaterialBuffer = matAddr,
                .quadOffset = dc.quadOffset,
                .quadCount = dc.quadCount,
                .atlasBindlessIndex = dc.atlasBindlessIndex,
                .textMaterialIndex = dc.textMaterialIndex,
                .sceneDataIndex = 0,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TextRenderPushConstant), &pc);
            vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1);
        }

        vkCmdEndRendering(cmd);
    });
}

void SetupSpritesPass(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets)
{
    if (viewFamily.spriteBatches.IsEmpty()) {
        return;
    }

    auto& spritesPass = graph.AddPass(SID("Sprites"), VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, Render::RenderCategory::Scene);
    spritesPass.ReadBuffer(SCENE_DATA_BUFFER);
    spritesPass.ReadBuffer(SPRITE_BUFFER);
    spritesPass.ReadWriteDepthAttachment(targets.depthStencil);
    spritesPass.WriteColorAttachment(targets.colorOutput);
    spritesPass.WriteColorAttachment(targets.stableId);
    spritesPass.Execute([&, width = renderExtent[0], height = renderExtent[1], pipelineManager, outputColor = targets.colorOutput, depthTarget = targets.depthStencil, stableId = targets.stableId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(outputColor), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo stableIdAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(stableId), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthTarget), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo colorAttachments[] = {colorAttachment, stableIdAttachment};
        VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 2, &depthAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("sprites"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);

        const VkDeviceAddress sceneDataAddr = graph.GetBufferAddress(SCENE_DATA_BUFFER);
        const VkDeviceAddress spritesAddr = graph.GetBufferAddress(SPRITE_BUFFER);

        for (const Core::SpriteBatch& batch : viewFamily.spriteBatches) {
            SpritePushConstant pc{
                .sceneData = sceneDataAddr,
                .sprites = spritesAddr,
                .spriteCount = batch.count,
                .spriteOffset = batch.offset,
                .textureIndex = batch.textureIndex,
                .samplerIndex = batch.samplerIndex,
                .sceneDataIndex = 0,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SpritePushConstant), &pc);

            const uint32_t groupCount = (batch.count + 31) / 32;
            vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1);
        }

        vkCmdEndRendering(cmd);
    });
}
} // Render
