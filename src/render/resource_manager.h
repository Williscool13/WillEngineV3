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
#include "editor/renderer/debug_readback_buffer.h"
#endif


namespace Render
{
struct ResourceManager
{
    ResourceManager();

    ~ResourceManager();

    explicit ResourceManager(VulkanContext* context);

    // Only managed by Asset Load Thread
    // OffsetAllocator::Allocator uses heap internally; acceptable as these are relatively rare/low frequency.
    std::mutex vertexBufferAllocatorMutex;
    OffsetAllocator::Allocator vertexBufferAllocator{MEGA_VERTEX_POSITION_BUFFER_SIZE};
    std::mutex meshletVertexBufferAllocatorMutex;
    OffsetAllocator::Allocator meshletVertexBufferAllocator{MEGA_MESHLET_VERTEX_BUFFER_SIZE};
    std::mutex meshletTriangleBufferAllocatorMutex;
    OffsetAllocator::Allocator meshletTriangleBufferAllocator{MEGA_MESHLET_TRIANGLE_BUFFER_SIZE};
    std::mutex meshletBufferAllocatorMutex;
    OffsetAllocator::Allocator meshletBufferAllocator{MEGA_MESHLET_BUFFER_SIZE};
    std::mutex primitiveBufferAllocatorMutex;
    OffsetAllocator::Allocator primitiveBufferAllocator{MEGA_PRIMITIVE_BUFFER_SIZE};

    // Managed by Asset Load, bound in the Render Threads. Synchronized by engine.
    AllocatedBuffer megaVertexPositionBuffer;
    AllocatedBuffer megaVertexAttributeBuffer;
    AllocatedBuffer megaMeshletVerticesBuffer;
    AllocatedBuffer megaMeshletTrianglesBuffer;
    AllocatedBuffer megaMeshletBuffer;
    AllocatedBuffer primitiveBuffer;
    BindlessResourcesSamplerImages bindlessSamplerTextureDescriptorBuffer{};

    // 1x1 black texture
    AllocatedImage blackDummyRG32Image;
    ImageView blackDummyRG32ImageView;

#if WILL_EDITOR
    Editor::EnvironmentMapGenerateResources environmentMapGenerateResources{};
    BindlessResourcesStorage<1> brdfLutGenerateResources{};
    BindlessResourcesStorage<1> smaaLookupGenerateResources{};
    BindlessResourcesStorage<1> smaaSearchGenerateResources{};
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
        RDG_MAX_STORAGE_UINT2,
        RDG_MAX_STORAGE_UINT,
        RDG_MAX_SAMPLED_FLOAT2,
        RDG_MAX_SAMPLED_FLOAT,
        RDG_MAX_SAMPLED_UINT4,
        RDG_MAX_SAMPLED_UINT2,
        RDG_MAX_SAMPLED_UINT,
        RDG_MAX_MULTISAMPLED_IMAGE,
        RDG_MAX_MULTISAMPLED_UINT_IMAGE
    > bindlessRDGTransientDescriptorBuffer{};

#if WILL_EDITOR
    Editor::DebugReadbackBuffer debugReadback;
#endif

    VulkanContext* context{};
};
} // Render

#endif //WILL_ENGINE_VK_RENDER_RESOURCES_H
