//
// Created by William on 2025-12-13.
//

#ifndef WILL_ENGINE_BASIC_COMPUTE_PIPELINE_H
#define WILL_ENGINE_BASIC_COMPUTE_PIPELINE_H

#include <glm/glm.hpp>

#include "render/vk_resources.h"

namespace Render
{
struct VulkanContext;

struct BasicComputePushConstant
{
    glm::vec4 color1;
    glm::vec4 color2;
    glm::ivec2 extent;
};

class BasicComputePipeline
{
public:
    BasicComputePipeline();

    ~BasicComputePipeline();

    explicit BasicComputePipeline(VulkanContext* context, DescriptorSetLayout& renderTargetSetLayout);

    BasicComputePipeline(const BasicComputePipeline&) = delete;

    BasicComputePipeline& operator=(const BasicComputePipeline&) = delete;

    BasicComputePipeline(BasicComputePipeline&& other) noexcept;

    BasicComputePipeline& operator=(BasicComputePipeline&& other) noexcept;

public:
    PipelineLayout pipelineLayout;
    Pipeline pipeline;

private:
    VulkanContext* context{};
};

} // Render

#endif //WILL_ENGINE_BASIC_COMPUTE_PIPELINE_H