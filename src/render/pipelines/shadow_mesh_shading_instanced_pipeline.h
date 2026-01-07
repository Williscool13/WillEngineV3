//
// Created by William on 2026-01-06.
//

#ifndef WILL_ENGINE_SHADOW_MESH_SHADING_INSTANCED_PIPELINE_H
#define WILL_ENGINE_SHADOW_MESH_SHADING_INSTANCED_PIPELINE_H
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

class ShadowMeshShadingInstancedPipeline
{
public:
    ShadowMeshShadingInstancedPipeline();

    ~ShadowMeshShadingInstancedPipeline();

    explicit ShadowMeshShadingInstancedPipeline(VulkanContext* context);

    ShadowMeshShadingInstancedPipeline(const ShadowMeshShadingInstancedPipeline&) = delete;

    ShadowMeshShadingInstancedPipeline& operator=(const ShadowMeshShadingInstancedPipeline&) = delete;

    ShadowMeshShadingInstancedPipeline(ShadowMeshShadingInstancedPipeline&& other) noexcept;

    ShadowMeshShadingInstancedPipeline& operator=(ShadowMeshShadingInstancedPipeline&& other) noexcept;

public:
    PipelineLayout pipelineLayout;
    Pipeline pipeline;

private:
    VulkanContext* context{nullptr};
};
} // Render

#endif //WILL_ENGINE_SHADOW_MESH_SHADING_INSTANCED_PIPELINE_H