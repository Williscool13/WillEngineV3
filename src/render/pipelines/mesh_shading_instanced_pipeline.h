//
// Created by William on 2025-12-30.
//

#ifndef WILL_ENGINE_MESH_SHADER_INSTANCED_PIPELINE_H
#define WILL_ENGINE_MESH_SHADER_INSTANCED_PIPELINE_H
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

class MeshShadingInstancedPipeline
{
public:
    MeshShadingInstancedPipeline();

    ~MeshShadingInstancedPipeline();

    MeshShadingInstancedPipeline(VulkanContext* context, DescriptorSetLayout& bindlessResources);

    MeshShadingInstancedPipeline(const MeshShadingInstancedPipeline&) = delete;

    MeshShadingInstancedPipeline& operator=(const MeshShadingInstancedPipeline&) = delete;

    MeshShadingInstancedPipeline(MeshShadingInstancedPipeline&& other) noexcept;

    MeshShadingInstancedPipeline& operator=(MeshShadingInstancedPipeline&& other) noexcept;

public:
    PipelineLayout pipelineLayout;
    Pipeline pipeline;

private:
    VulkanContext* context{nullptr};
};
class mesh_shading_instanced_pipeline
{};
} // Render

#endif //WILL_ENGINE_MESH_SHADER_INSTANCED_PIPELINE_H