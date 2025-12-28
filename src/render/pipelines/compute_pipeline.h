//
// Created by William on 2025-12-28.
//

#ifndef WILL_ENGINE_COMPUTE_PIPELINE_H
#define WILL_ENGINE_COMPUTE_PIPELINE_H
#include <filesystem>

#include "render/vulkan/vk_resources.h"

namespace Render
{
class ComputePipeline
{
public:
    ComputePipeline();

    ~ComputePipeline();

    explicit ComputePipeline(VulkanContext* context, VkPipelineLayoutCreateInfo layoutCreateInfo, std::filesystem::path shaderSource);

    ComputePipeline(const ComputePipeline&) = delete;

    ComputePipeline& operator=(const ComputePipeline&) = delete;

    ComputePipeline(ComputePipeline&& other) noexcept;

    ComputePipeline& operator=(ComputePipeline&& other) noexcept;

public:
    PipelineLayout pipelineLayout;
    Pipeline pipeline;

private:
    VulkanContext* context{};
};
} // Render

#endif //WILL_ENGINE_COMPUTE_PIPELINE_H