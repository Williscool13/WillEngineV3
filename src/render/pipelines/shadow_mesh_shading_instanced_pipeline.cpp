//
// Created by William on 2026-01-06.
//

#include "shadow_mesh_shading_instanced_pipeline.h"

#include "vk_pipeline.h"
#include "render/shaders/push_constant_interop.h"
#include "render/vulkan/vk_config.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
ShadowMeshShadingInstancedPipeline::ShadowMeshShadingInstancedPipeline() = default;

ShadowMeshShadingInstancedPipeline::~ShadowMeshShadingInstancedPipeline() = default;

ShadowMeshShadingInstancedPipeline::ShadowMeshShadingInstancedPipeline(VulkanContext* context)
{
    VkPushConstantRange renderPushConstantRange{};
    renderPushConstantRange.offset = 0;
    renderPushConstantRange.size = sizeof(ShadowMeshShadingPushConstant);
    renderPushConstantRange.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.pSetLayouts = nullptr;
    pipelineLayoutCreateInfo.setLayoutCount = 0;
    pipelineLayoutCreateInfo.pPushConstantRanges = &renderPushConstantRange;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;

    pipelineLayout = PipelineLayout::CreatePipelineLayout(context, pipelineLayoutCreateInfo);
    pipelineLayout.SetDebugName("Cascaded Shadow Map Pipeline Layout");

    VkShaderModule taskShader;
    VkShaderModule meshShader;
    if (!VkHelpers::LoadShaderModule("shaders\\shadowMeshShadingInstanced_task.spv", context->device, &taskShader)) {
        SPDLOG_ERROR("Failed to load meshShading_task.spv");
        return;
    }
    if (!VkHelpers::LoadShaderModule("shaders\\shadowMeshShadingInstanced_mesh.spv", context->device, &meshShader)) {
        SPDLOG_ERROR("Failed to load meshShading_mesh.spv");
        return;
    }

    RenderPipelineBuilder pipelineBuilder;
    VkPipelineShaderStageCreateInfo shaderStages[2];
    shaderStages[0] = VkHelpers::PipelineShaderStageCreateInfo(taskShader, VK_SHADER_STAGE_TASK_BIT_EXT);
    shaderStages[1] = VkHelpers::PipelineShaderStageCreateInfo(meshShader, VK_SHADER_STAGE_MESH_BIT_EXT);

    pipelineBuilder.SetShaders(shaderStages, 2);
    pipelineBuilder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.EnableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipelineBuilder.SetupRenderer(nullptr, 0, SHADOW_CASCADE_FORMAT);
    pipelineBuilder.SetupPipelineLayout(pipelineLayout.handle);
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = pipelineBuilder.GeneratePipelineCreateInfo();
    pipeline = Pipeline::CreateGraphicsPipeline(context, pipelineCreateInfo);
    pipeline.SetDebugName("Cascaded Shadow Map Pipeline");

    vkDestroyShaderModule(context->device, taskShader, nullptr);
    vkDestroyShaderModule(context->device, meshShader, nullptr);
}

ShadowMeshShadingInstancedPipeline::ShadowMeshShadingInstancedPipeline(ShadowMeshShadingInstancedPipeline&& other) noexcept
{
    pipelineLayout = std::move(other.pipelineLayout);
    pipeline = std::move(other.pipeline);
    context = other.context;
    other.context = nullptr;
}

ShadowMeshShadingInstancedPipeline& ShadowMeshShadingInstancedPipeline::operator=(ShadowMeshShadingInstancedPipeline&& other) noexcept
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