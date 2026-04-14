//
// Created by William on 2026-01-24.
//

#ifndef WILL_ENGINE_PIPELINE_DATA_H
#define WILL_ENGINE_PIPELINE_DATA_H

#include <volk.h>

#include "core/containers/inline_path.h"
#include "core/containers/inline_vector.h"
#include "core/string_id.h"
#include "pipeline_category.h"
#include "core/memory/memory_manager.h"

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
    virtual bool CreatePipeline(VulkanContext* ctx, Core::MemoryManager* memoryManager, VkPipelineCache pipelineCache) = 0;

    // Initialized once, never modified again
    StringID pipelineId{};
    PipelineCategory category{PipelineCategory::None};
    VkPipelineLayoutCreateInfo layoutCreateInfo{};
    Core::InlineVector<VkDescriptorSetLayout, 8> customLayout{};

    VkPushConstantRange pushConstantRange{};

    // If true, loadingEntry is managed by asset load thead, do not touch.
    bool bLoading{false};
    PipelineEntry loadingEntry{};

    PipelineEntry activeEntry{};
    uint64_t lastModified{0};

    PipelineEntry retiredEntry{};
    uint32_t retirementFrame{0};
};

class ComputePipelineData : public PipelineData
{
public:
    ~ComputePipelineData() override = default;
    bool CreatePipeline(VulkanContext* context, Core::MemoryManager* memoryManager, VkPipelineCache pipelineCache) override;

    Core::Path shaderPath{};
};

class GraphicsPipelineData : public PipelineData
{
public:
    ~GraphicsPipelineData() override = default;
    bool CreatePipeline(VulkanContext* context, Core::MemoryManager* memoryManager, VkPipelineCache pipelineCache) override;

    static constexpr uint32_t MAX_SHADER_STAGES = 5;
    static constexpr uint32_t MAX_VERTEX_BINDINGS = 8;
    static constexpr uint32_t MAX_VERTEX_ATTRIBUTES = 16;
    static constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;
    static constexpr uint32_t MAX_DYNAMIC_STATES = 16;

    Core::InlineVector<Core::Path, MAX_SHADER_STAGES> shaderPaths{};
    Core::InlineVector<VkPipelineShaderStageCreateInfo, MAX_SHADER_STAGES> shaderStages{};

    Core::InlineVector<VkVertexInputBindingDescription, MAX_VERTEX_BINDINGS> vertexBindings{};
    Core::InlineVector<VkVertexInputAttributeDescription, MAX_VERTEX_ATTRIBUTES> vertexAttributes{};

    Core::InlineVector<VkFormat, MAX_COLOR_ATTACHMENTS> colorAttachmentFormats{};

    Core::InlineVector<VkPipelineColorBlendAttachmentState, MAX_COLOR_ATTACHMENTS> blendAttachmentStates{};

    Core::InlineVector<VkDynamicState, MAX_DYNAMIC_STATES> dynamicStates{};

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