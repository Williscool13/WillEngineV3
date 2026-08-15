//
// Created by William on 2026-06-03.
//

#include "render/passes/ui_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupUIRender(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, StringID targetImage)
{
    if (viewFamily.uiDrawList.IsEmpty()) { return; }

    const bool bHasText = !viewFamily.uiGlyphQuads.IsEmpty() && graph.HasBuffer(UI_GLYPH_QUAD_BUFFER) && graph.HasBuffer(FONT_CURVE_BUFFER);

    RenderPass& uiPass = graph.AddPass(SID("UI Render"), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::UI);
    if (bHasText) {
        uiPass.ReadBuffer(UI_GLYPH_QUAD_BUFFER);
        uiPass.ReadBuffer(FONT_CURVE_BUFFER);
    }
    uiPass.WriteColorAttachment(targetImage);
    uiPass.Execute([&, width = renderExtent[0], height = renderExtent[1], targetImage, pipelineManager, bHasText](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        // Y-flipped viewport: blit to swapchain inverts Y, so pre-invert here to cancel it out
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        viewport.y = static_cast<float>(height);
        viewport.height = -static_cast<float>(height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        const VkRect2D fullScissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &fullScissor);

        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targetImage), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, &colorAttachment, 1, nullptr, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);

        const VkDeviceAddress glyphQuadsAddr = bHasText ? graph.GetBufferAddress(UI_GLYPH_QUAD_BUFFER) : 0;
        const VkDeviceAddress fontCurveAddr = bHasText ? graph.GetBufferAddress(FONT_CURVE_BUFFER) : 0;

        const PipelineEntry* rectPipeline = pipelineManager->GetPipelineEntry(SID("ui_rect_default"));
        const PipelineEntry* imagePipeline = pipelineManager->GetPipelineEntry(SID("ui_image_default"));
        const PipelineEntry* textPipeline = pipelineManager->GetPipelineEntry(SID("ui_text_default"));
        const PipelineEntry* borderPipeline = pipelineManager->GetPipelineEntry(SID("ui_border_default"));

        Core::UICommandType boundPipeline = Core::UICommandType::ScissorPop; // sentinel: nothing bound
        glm::vec4 overlayColor{1.0f, 1.0f, 1.0f, 1.0f};

        for (const Core::UIDrawCommand& drawCmd : viewFamily.uiDrawList) {
            switch (drawCmd.type) {
                case Core::UICommandType::ScissorPush:
                {
                    const Core::UIScissorCommand& s = drawCmd.scissor;
                    int32_t x = s.x;
                    int32_t y = static_cast<int32_t>(height) - s.y - static_cast<int32_t>(s.height);
                    int32_t w = static_cast<int32_t>(s.width);
                    int32_t h = static_cast<int32_t>(s.height);
                    if (x < 0) { w += x; x = 0; }
                    if (y < 0) { h += y; y = 0; }
                    if (x + w > static_cast<int32_t>(width)) { w = static_cast<int32_t>(width) - x; }
                    if (y + h > static_cast<int32_t>(height)) { h = static_cast<int32_t>(height) - y; }
                    w = w > 0 ? w : 0;
                    h = h > 0 ? h : 0;
                    const VkRect2D scissor{{x, y}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}};
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    break;
                }
                case Core::UICommandType::ScissorPop:
                {
                    vkCmdSetScissor(cmd, 0, 1, &fullScissor);
                    break;
                }
                case Core::UICommandType::OverlayPush:
                {
                    const float4& c = drawCmd.overlay.color;
                    overlayColor = {c.x, c.y, c.z, c.w};
                    break;
                }
                case Core::UICommandType::OverlayPop:
                {
                    overlayColor = {1.0f, 1.0f, 1.0f, 1.0f};
                    break;
                }
                case Core::UICommandType::Rect:
                {
                    if (boundPipeline != Core::UICommandType::Rect) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rectPipeline->pipeline);
                        boundPipeline = Core::UICommandType::Rect;
                    }
                    const Core::UIRectDrawCall& r = drawCmd.rect;
                    const float fw = static_cast<float>(width);
                    const float fh = static_cast<float>(height);
                    UIRectRenderPushConstant pc{
                        .color = {r.color.x * overlayColor.x, r.color.y * overlayColor.y, r.color.z * overlayColor.z, r.color.w * overlayColor.w},
                        .ndcMin = {r.pxMin.x / fw * 2.0f - 1.0f, r.pxMin.y / fh * 2.0f - 1.0f},
                        .ndcMax = {r.pxMax.x / fw * 2.0f - 1.0f, r.pxMax.y / fh * 2.0f - 1.0f},
                        .cornerRadius = r.cornerRadius,
                        .pxMin = r.pxMin,
                        .pxMax = r.pxMax,
                    };
                    vkCmdPushConstants(cmd, rectPipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIRectRenderPushConstant), &pc);
                    vkCmdDraw(cmd, 4, 1, 0, 0);
                    break;
                }
                case Core::UICommandType::Image:
                {
                    if (boundPipeline != Core::UICommandType::Image) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, imagePipeline->pipeline);
                        boundPipeline = Core::UICommandType::Image;
                    }
                    const Core::UIRenderCommandImage& uiCmd = drawCmd.image;
                    const float fw = static_cast<float>(width);
                    const float fh = static_cast<float>(height);
                    UIImagePushConstant pc{
                        .ndcMin = {uiCmd.pxMin.x / fw * 2.0f - 1.0f, uiCmd.pxMin.y / fh * 2.0f - 1.0f},
                        .ndcMax = {uiCmd.pxMax.x / fw * 2.0f - 1.0f, uiCmd.pxMax.y / fh * 2.0f - 1.0f},
                        .uvMin = {uiCmd.uvMin.x, uiCmd.uvMin.y},
                        .uvMax = {uiCmd.uvMax.x, uiCmd.uvMax.y},
                        .tintColor = {uiCmd.tintColor.x * overlayColor.x, uiCmd.tintColor.y * overlayColor.y, uiCmd.tintColor.z * overlayColor.z, uiCmd.tintColor.w * overlayColor.w},
                        .cornerRadius = uiCmd.cornerRadius,
                        .pxMin = uiCmd.pxMin,
                        .pxMax = uiCmd.pxMax,
                        .imageBindlessIndex = uiCmd.imageBindlessIndex,
                    };
                    vkCmdPushConstants(cmd, imagePipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIImagePushConstant), &pc);
                    vkCmdDraw(cmd, 4, 1, 0, 0);
                    break;
                }
                case Core::UICommandType::Border:
                {
                    if (boundPipeline != Core::UICommandType::Border) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, borderPipeline->pipeline);
                        boundPipeline = Core::UICommandType::Border;
                    }
                    const Core::UIBorderDrawCall& b = drawCmd.border;
                    const float fw = static_cast<float>(width);
                    const float fh = static_cast<float>(height);
                    UIBorderPushConstant pc{
                        .ndcMin = {b.pxMin.x / fw * 2.0f - 1.0f, b.pxMin.y / fh * 2.0f - 1.0f},
                        .ndcMax = {b.pxMax.x / fw * 2.0f - 1.0f, b.pxMax.y / fh * 2.0f - 1.0f},
                        .color = {b.color.x * overlayColor.x, b.color.y * overlayColor.y, b.color.z * overlayColor.z, b.color.w * overlayColor.w},
                        .borderWidths = b.borderWidths,
                        .cornerRadius = b.cornerRadius,
                        .pxMin = b.pxMin,
                        .pxMax = b.pxMax,
                    };
                    vkCmdPushConstants(cmd, borderPipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIBorderPushConstant), &pc);
                    vkCmdDraw(cmd, 4, 1, 0, 0);
                    break;
                }
                case Core::UICommandType::Text:
                {
                    if (!bHasText) { break; }
                    if (boundPipeline != Core::UICommandType::Text) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipeline->pipeline);
                        boundPipeline = Core::UICommandType::Text;
                    }
                    UITextRenderPushConstant pc{
                        .uiGlyphQuads = glyphQuadsAddr,
                        .fontCurveTexels = fontCurveAddr + drawCmd.text.fontCurveByteOffset,
                        .colorTint = {overlayColor.x, overlayColor.y, overlayColor.z, overlayColor.w},
                        .quadOffset = drawCmd.text.quadOffset,
                        .quadCount = drawCmd.text.quadCount,
                    };
                    vkCmdPushConstants(cmd, textPipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UITextRenderPushConstant), &pc);
                    vkCmdDraw(cmd, 4, drawCmd.text.quadCount, 0, 0);
                    break;
                }
            }
        }

        vkCmdEndRendering(cmd);
    });
}

void SetupSelectionOutlinePass(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint64_t selectedStableId)
{
    RenderPass& outlinePass = graph.AddPass(SID("Selection Outline"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::UI);
    outlinePass.ReadStorageImage(targets.stableId);
    outlinePass.WriteStorageImage(targets.colorOutput);
    outlinePass.Execute([&, pipelineManager, renderExtent, selectedStableId, stableId = targets.stableId, outputColor = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("selection_outline"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        SelectionOutlinePushConstant pc{
            .selectedStableIdLo = static_cast<uint32_t>(selectedStableId & 0xFFFFFFFFu),
            .selectedStableIdHi = static_cast<uint32_t>(selectedStableId >> 32u),
            .extents = {renderExtent[0], renderExtent[1]},
            .stableIdIndex = graph.GetStorageImageViewDescriptorIndex(stableId),
            .outputColorIndex = graph.GetStorageImageViewDescriptorIndex(outputColor),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15) / 16, (renderExtent[1] + 15) / 16, 1);
    });
}
} // Render
