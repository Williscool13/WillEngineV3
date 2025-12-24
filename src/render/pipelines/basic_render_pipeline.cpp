//
// Created by William on 2025-12-13.
//

#include "basic_render_pipeline.h"

#include "vk_pipeline.h"
#include "platform/paths.h"
#include "render/vulkan/vk_config.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
BasicRenderPipeline::BasicRenderPipeline() = default;

BasicRenderPipeline::~BasicRenderPipeline() = default;

BasicRenderPipeline::BasicRenderPipeline(VulkanContext* context) : context(context)
{
    VkPushConstantRange renderPushConstantRange{};
    renderPushConstantRange.offset = 0;
    renderPushConstantRange.size = sizeof(BasicRenderPushConstant);
    renderPushConstantRange.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.pSetLayouts = nullptr;
    pipelineLayoutCreateInfo.setLayoutCount = 0;
    pipelineLayoutCreateInfo.pPushConstantRanges = &renderPushConstantRange;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;

    pipelineLayout = PipelineLayout::CreatePipelineLayout(context, pipelineLayoutCreateInfo);
    pipelineLayout.SetDebugName("Basic Mesh Render Pipeline Layout");

    VkShaderModule taskShader;
    VkShaderModule meshShader;
    VkShaderModule fragShader;
    if (!VkHelpers::LoadShaderModule("shaders\\basicRender_task.spv", context->device, &taskShader)) {
        SPDLOG_ERROR("Failed to load basicRender_task.spv");
        return;
    }
    if (!VkHelpers::LoadShaderModule("shaders\\basicRender_mesh.spv", context->device, &meshShader)) {
        SPDLOG_ERROR("Failed to load basicRender_mesh.spv");
        return;
    }
    if (!VkHelpers::LoadShaderModule("shaders\\basicRender_fragment.spv", context->device, &fragShader)) {
        SPDLOG_ERROR("Failed to load basicRender_fragment.spv");
        return;
    }


    RenderPipelineBuilder pipelineBuilder;
    VkPipelineShaderStageCreateInfo shaderStages[3];
    shaderStages[0] = VkHelpers::PipelineShaderStageCreateInfo(taskShader, VK_SHADER_STAGE_TASK_BIT_EXT);
    shaderStages[1] = VkHelpers::PipelineShaderStageCreateInfo(meshShader, VK_SHADER_STAGE_MESH_BIT_EXT);
    shaderStages[2] = VkHelpers::PipelineShaderStageCreateInfo(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT);
    pipelineBuilder.SetShaders(shaderStages, 3);
    pipelineBuilder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.EnableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    VkFormat colorFormats[1] = {COLOR_ATTACHMENT_FORMAT};
    pipelineBuilder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT);
    pipelineBuilder.SetupPipelineLayout(pipelineLayout.handle);
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = pipelineBuilder.GeneratePipelineCreateInfo();
    pipeline = Pipeline::CreateGraphicsPipeline(context, pipelineCreateInfo);
    pipeline.SetDebugName("Basic Mesh Render Pipeline");

    vkDestroyShaderModule(context->device, taskShader, nullptr);
    vkDestroyShaderModule(context->device, meshShader, nullptr);
    vkDestroyShaderModule(context->device, fragShader, nullptr);
}

BasicRenderPipeline::BasicRenderPipeline(BasicRenderPipeline&& other) noexcept
{
    pipelineLayout = std::move(other.pipelineLayout);
    pipeline = std::move(other.pipeline);
    context = other.context;
    other.context = nullptr;
}

BasicRenderPipeline& BasicRenderPipeline::operator=(BasicRenderPipeline&& other) noexcept
{
    if (this != &other) {
        pipelineLayout = std::move(other.pipelineLayout);
        pipeline = std::move(other.pipeline);
        context = other.context;
        other.context = nullptr;
    }
    return *this;
}
} // Render
