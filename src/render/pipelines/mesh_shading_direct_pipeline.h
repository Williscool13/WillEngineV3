//
// Created by William on 2026-01-23.
//

#ifndef WILL_ENGINE_MESH_SHADING_DIRECT_PIPELINE_H
#define WILL_ENGINE_MESH_SHADING_DIRECT_PIPELINE_H

#include <array>

#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

class MeshShadingDirectPipeline
{
public:
    MeshShadingDirectPipeline();

    ~MeshShadingDirectPipeline();

    MeshShadingDirectPipeline(VulkanContext* context, std::array<VkDescriptorSetLayout, 2> descriptorSets);

    MeshShadingDirectPipeline(const MeshShadingDirectPipeline&) = delete;

    MeshShadingDirectPipeline& operator=(const MeshShadingDirectPipeline&) = delete;

    MeshShadingDirectPipeline(MeshShadingDirectPipeline&& other) noexcept;

    MeshShadingDirectPipeline& operator=(MeshShadingDirectPipeline&& other) noexcept;

public:
    PipelineLayout pipelineLayout;
    Pipeline pipeline;

private:
    VulkanContext* context{nullptr};
};
} // Render

#endif //WILL_ENGINE_MESH_SHADING_DIRECT_PIPELINE_H
