//
// Created by William on 2025-12-30.
//

#include "mesh_shading_instanced_pipeline.h"

#include <algorithm>

#include "vk_pipeline.h"
#include "render/shaders/push_constant_interop.h"
#include "render/vulkan/vk_config.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
MeshShadingInstancedPipeline::MeshShadingInstancedPipeline() = default;

MeshShadingInstancedPipeline::~MeshShadingInstancedPipeline() = default;

MeshShadingInstancedPipeline::MeshShadingInstancedPipeline(VulkanContext* context, std::array<VkDescriptorSetLayout, 2> descriptorSets)
{
    VkPushConstantRange renderPushConstantRange{};
    renderPushConstantRange.offset = 0;
    renderPushConstantRange.size = sizeof(InstancedMeshShadingPushConstant);
    renderPushConstantRange.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSets.data();
    pipelineLayoutCreateInfo.setLayoutCount = descriptorSets.size();
    pipelineLayoutCreateInfo.pPushConstantRanges = &renderPushConstantRange;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;

    pipelineLayout = PipelineLayout::CreatePipelineLayout(context, pipelineLayoutCreateInfo);
    pipelineLayout.SetDebugName("Mesh Shader Pipeline Layout");

    VkShaderModule taskShader;
    VkShaderModule meshShader;
    VkShaderModule fragShader;
    if (!VkHelpers::LoadShaderModule("shaders\\mesh_shading_instanced_task.spv", context->device, &taskShader)) {
        SPDLOG_ERROR("Failed to load meshShading_task.spv");
        return;
    }
    if (!VkHelpers::LoadShaderModule("shaders\\mesh_shading_instanced_mesh.spv", context->device, &meshShader)) {
        SPDLOG_ERROR("Failed to load meshShading_mesh.spv");
        return;
    }
    if (!VkHelpers::LoadShaderModule("shaders\\mesh_shading_instanced_fragment.spv", context->device, &fragShader)) {
        SPDLOG_ERROR("Failed to load meshShading_fragment.spv");
        return;
    }

    RenderPipelineBuilder pipelineBuilder;
    VkPipelineShaderStageCreateInfo shaderStages[3];
    shaderStages[0] = VkHelpers::PipelineShaderStageCreateInfo(taskShader, VK_SHADER_STAGE_TASK_BIT_EXT);
    shaderStages[1] = VkHelpers::PipelineShaderStageCreateInfo(meshShader, VK_SHADER_STAGE_MESH_BIT_EXT);
    shaderStages[2] = VkHelpers::PipelineShaderStageCreateInfo(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT);

    pipelineBuilder.SetShaders(shaderStages, 3);
    pipelineBuilder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.EnableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    VkFormat colorFormats[5] = {GBUFFER_ALBEDO_FORMAT, GBUFFER_NORMAL_FORMAT, GBUFFER_PBR_FORMAT, GBUFFER_EMISSIVE_FORMAT, GBUFFER_MOTION_FORMAT};
    pipelineBuilder.SetupRenderer(colorFormats, 5, DEPTH_ATTACHMENT_FORMAT);
    pipelineBuilder.SetupPipelineLayout(pipelineLayout.handle);
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = pipelineBuilder.GeneratePipelineCreateInfo();
    pipeline = Pipeline::CreateGraphicsPipeline(context, pipelineCreateInfo);
    pipeline.SetDebugName("Mesh Shader Pipeline");

    vkDestroyShaderModule(context->device, taskShader, nullptr);
    vkDestroyShaderModule(context->device, meshShader, nullptr);
    vkDestroyShaderModule(context->device, fragShader, nullptr);
}

MeshShadingInstancedPipeline::MeshShadingInstancedPipeline(MeshShadingInstancedPipeline&& other) noexcept
{
    pipelineLayout = std::move(other.pipelineLayout);
    pipeline = std::move(other.pipeline);
    context = other.context;
    other.context = nullptr;
}

MeshShadingInstancedPipeline& MeshShadingInstancedPipeline::operator=(MeshShadingInstancedPipeline&& other) noexcept
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
