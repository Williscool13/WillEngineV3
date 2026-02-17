//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_RENDER_RESOURCES_H
#define WILL_ENGINE_VK_RENDER_RESOURCES_H

#include "offsetAllocator.hpp"
#include "descriptors/vk_bindless_transient_rdg_resources.h"
#include "render/render_config.h"
#include "vulkan/vk_resources.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/descriptors/vk_bindless_resources_storage.h"
#include "render-graph/render_graph_resources.h"

#if WILL_EDITOR
#include "editor/asset-generation/environment_map_generate_resources.h"
#endif


namespace Render
{
struct ResourceManager
{
    ResourceManager();

    ~ResourceManager();

    explicit ResourceManager(VulkanContext* context);

    // Only managed by Asset Load Thread
    std::mutex vertexBufferAllocatorMutex;
    OffsetAllocator::Allocator vertexBufferAllocator{MEGA_VERTEX_BUFFER_SIZE};
    std::mutex meshletVertexBufferAllocatorMutex;
    OffsetAllocator::Allocator meshletVertexBufferAllocator{MEGA_MESHLET_VERTEX_BUFFER_SIZE};
    std::mutex meshletTriangleBufferAllocatorMutex;
    OffsetAllocator::Allocator meshletTriangleBufferAllocator{MEGA_MESHLET_TRIANGLE_BUFFER_SIZE};
    std::mutex meshletBufferAllocatorMutex;
    OffsetAllocator::Allocator meshletBufferAllocator{MEGA_MESHLET_BUFFER_SIZE};
    std::mutex primitiveBufferAllocatorMutex;
    OffsetAllocator::Allocator primitiveBufferAllocator{MEGA_PRIMITIVE_BUFFER_SIZE};

    // Managed by Asset Load, bound in the Render Threads. Synchronized by engine.
    AllocatedBuffer megaVertexBuffer;
    AllocatedBuffer megaMeshletVerticesBuffer;
    AllocatedBuffer megaMeshletTrianglesBuffer;
    AllocatedBuffer megaMeshletBuffer;
    AllocatedBuffer primitiveBuffer;
    BindlessResourcesSamplerImages bindlessSamplerTextureDescriptorBuffer{};

#if WILL_EDITOR
    Editor::EnvironmentMapGenerateResources environmentMapGenerateResources{};
    BindlessResourcesStorage<1> brdfLutGenerateResources{};
#endif

    VkSampler pointSampler;
    VkSampler linearSampler;
    VkSampler depthCompareSampler;
    BindlessTransientRDGResourcesDescriptorBuffer<
        4,
        4,
        RDG_MAX_SAMPLED_TEXTURES,
        RDG_MAX_STORAGE_FLOAT4,
        RDG_MAX_STORAGE_FLOAT2,
        RDG_MAX_STORAGE_FLOAT,
        RDG_MAX_STORAGE_UINT4,
        RDG_MAX_STORAGE_UINT
    > bindlessRDGTransientDescriptorBuffer{};

    AllocatedBuffer debugReadbackBuffer;
    PipelineEvent debugReadbackLastKnownState;

    VulkanContext* context{};
};
} // Render

#endif //WILL_ENGINE_VK_RENDER_RESOURCES_H
