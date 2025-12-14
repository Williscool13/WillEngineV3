//
// Created by William on 2025-12-13.
//

#ifndef WILL_ENGINE_BASIC_RENDER_PIPELINE_H
#define WILL_ENGINE_BASIC_RENDER_PIPELINE_H

#include <glm/glm.hpp>

#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

struct BasicRenderPushConstant
{
    glm::mat4 modelMatrix;
    VkDeviceAddress sceneData;
};

class BasicRenderPipeline
{
public:
    BasicRenderPipeline();

    ~BasicRenderPipeline();

    explicit BasicRenderPipeline(VulkanContext* context);

    BasicRenderPipeline(const BasicRenderPipeline&) = delete;

    BasicRenderPipeline& operator=(const BasicRenderPipeline&) = delete;

    BasicRenderPipeline(BasicRenderPipeline&& other) noexcept;

    BasicRenderPipeline& operator=(BasicRenderPipeline&& other) noexcept;

public:
    PipelineLayout pipelineLayout;
    Pipeline pipeline;

private:
    VulkanContext* context;
};
} // Render

#endif //WILL_ENGINE_BASIC_RENDER_PIPELINE_H