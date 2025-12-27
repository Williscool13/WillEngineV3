//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_RENDER_RESOURCES_H
#define WILL_ENGINE_VK_RENDER_RESOURCES_H

#include "offsetAllocator.hpp"
#include "descriptors/vk_bindless_transient_rdg_resources.h"
#include "render/render_config.h"
#include "vulkan/vk_resources.h"
#include "render/frame_resources.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/descriptors/vk_bindless_resources_storage.h"


namespace Render
{
struct ResourceManager
{
    ResourceManager();

    ~ResourceManager();

    explicit ResourceManager(VulkanContext* context);

    // Only managed by Asset Load Thread
    OffsetAllocator::Allocator vertexBufferAllocator{MEGA_VERTEX_BUFFER_SIZE};
    OffsetAllocator::Allocator skinnedVertexBufferAllocator{MEGA_SKINNED_VERTEX_BUFFER_SIZE};
    OffsetAllocator::Allocator meshletVertexBufferAllocator{MEGA_MESHLET_VERTEX_BUFFER_SIZE};
    OffsetAllocator::Allocator meshletTriangleBufferAllocator{MEGA_MESHLET_TRIANGLE_BUFFER_SIZE};
    OffsetAllocator::Allocator meshletBufferAllocator{MEGA_MESHLET_BUFFER_SIZE};
    OffsetAllocator::Allocator primitiveBufferAllocator{MEGA_PRIMITIVE_BUFFER_SIZE};

    // Managed by Asset Load, bound in the Render Threads. Synchronized by engine.
    AllocatedBuffer megaVertexBuffer;
    AllocatedBuffer megaSkinnedVertexBuffer;
    AllocatedBuffer megaMeshletVerticesBuffer;
    AllocatedBuffer megaMeshletTrianglesBuffer;
    AllocatedBuffer megaMeshletBuffer;
    AllocatedBuffer primitiveBuffer;
    BindlessResourcesSamplerImages bindlessSamplerTextureDescriptorBuffer{};
    BindlessResourcesStorage<8> bindlessRenderTargetDescriptorBuffer{};
    BindlessResourcesStorage<512> bindlessStorageDescriptorBuffer{};

    BindlessTransientRDGResourcesDescriptorBuffer<8, 128, 128> bindlessRDGTransientDescriptorBuffer{};

    std::array<FrameResources, Core::FRAME_BUFFER_COUNT> frameResources;

private:
    VulkanContext* context{};
};
} // Render

#endif //WILL_ENGINE_VK_RENDER_RESOURCES_H
