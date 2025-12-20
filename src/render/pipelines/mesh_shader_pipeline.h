//
// Created by William on 2025-11-18.
//

#ifndef WILLENGINETESTBED_MAIN_MESH_SHADER_PIPELINE_H
#define WILLENGINETESTBED_MAIN_MESH_SHADER_PIPELINE_H

#include <volk/volk.h>
#include <glm/glm.hpp>

#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

struct MeshShaderPushConstants
{
    VkDeviceAddress sceneData;

    // Statics
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress primitiveBuffer;
    VkDeviceAddress meshletVerticesBuffer;
    VkDeviceAddress meshletTrianglesBuffer;
    VkDeviceAddress meshletBuffer;

    // Dynamics
    VkDeviceAddress materialBuffer;
    VkDeviceAddress modelBuffer;
    VkDeviceAddress instanceBuffer;

    uint32_t instanceIndex;;
};


class MeshShaderPipeline
{
public:
    MeshShaderPipeline();

    ~MeshShaderPipeline();

    MeshShaderPipeline(VulkanContext* context, DescriptorSetLayout& sampleTextureSetLayout);

    MeshShaderPipeline(const MeshShaderPipeline&) = delete;

    MeshShaderPipeline& operator=(const MeshShaderPipeline&) = delete;

    MeshShaderPipeline(MeshShaderPipeline&& other) noexcept;

    MeshShaderPipeline& operator=(MeshShaderPipeline&& other) noexcept;

public:
    PipelineLayout pipelineLayout;
    Pipeline pipeline;

private:
    VulkanContext* context{nullptr};
};
} // Renderer

#endif //WILLENGINETESTBED_MAIN_MESH_SHADER_PIPELINE_H