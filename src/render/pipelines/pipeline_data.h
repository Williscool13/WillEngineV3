//
// Created by William on 2026-01-24.
//

#ifndef WILL_ENGINE_PIPELINE_DATA_H
#define WILL_ENGINE_PIPELINE_DATA_H

#include <filesystem>
#include <volk.h>

#include "pipeline_category.h"

namespace Render
{
struct VulkanContext;

struct PipelineEntry
{
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
};

class PipelineData
{
public:
    virtual ~PipelineData() = default;
    virtual bool CreatePipeline(VulkanContext* ctx, VkPipelineCache pipelineCache) = 0;

    // Initialized once, never modified again
    PipelineCategory category{PipelineCategory::None};
    VkPipelineLayoutCreateInfo layoutCreateInfo{};
    VkPushConstantRange pushConstantRange{};

    // If true, loadingEntry is managed by asset load thead, do not touch.
    bool bLoading{false};
    PipelineEntry loadingEntry{};

    PipelineEntry activeEntry{};
    std::filesystem::file_time_type lastModified{};

    PipelineEntry retiredEntry{};
    uint32_t retirementFrame{0};
};

class ComputePipelineData : public PipelineData
{
public:
    ~ComputePipelineData() override = default;
    bool CreatePipeline(VulkanContext* context, VkPipelineCache pipelineCache) override;

    std::filesystem::path shaderPath{};
    VkPipelineShaderStageCreateInfo shaderStage{};
};

class GraphicsPipelineData : public PipelineData
{
public:
    ~GraphicsPipelineData() override = default;
    bool CreatePipeline(VulkanContext* context, VkPipelineCache pipelineCache) override;

    static constexpr uint32_t MAX_SHADER_STAGES = 5;
    static constexpr uint32_t MAX_VERTEX_BINDINGS = 8;
    static constexpr uint32_t MAX_VERTEX_ATTRIBUTES = 16;
    static constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;
    static constexpr uint32_t MAX_DYNAMIC_STATES = 16;

    std::filesystem::path shaderPaths[MAX_SHADER_STAGES];

    VkPipelineShaderStageCreateInfo shaderStages[MAX_SHADER_STAGES];
    uint32_t shaderStageCount{0};

    VkVertexInputBindingDescription vertexBindings[MAX_VERTEX_BINDINGS];
    uint32_t vertexBindingCount{0};
    VkVertexInputAttributeDescription vertexAttributes[MAX_VERTEX_ATTRIBUTES];
    uint32_t vertexAttributeCount{0};

    VkFormat colorAttachmentFormats[MAX_COLOR_ATTACHMENTS];
    uint32_t colorAttachmentFormatCount{0};

    VkPipelineColorBlendAttachmentState blendAttachmentStates[MAX_COLOR_ATTACHMENTS];
    uint32_t blendAttachmentStateCount{0};

    VkDynamicState dynamicStates[MAX_DYNAMIC_STATES];
    uint32_t dynamicStateCount{2};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    };

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
    };

    VkPipelineRenderingCreateInfo renderInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    };

    VkPipelineTessellationStateCreateInfo tessellation{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
    };

    VkPipelineDynamicStateCreateInfo dynamicInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    };

    bool bIsTessellationEnabled{false};
};


} // Render

#endif //WILL_ENGINE_PIPELINE_DATA_H