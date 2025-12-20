//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_RENDER_RESOURCES_H
#define WILL_ENGINE_VK_RENDER_RESOURCES_H

#include "offsetAllocator.hpp"
#include "render/render_config.h"
#include "vk_resources.h"
#include "core/allocators/free_list.h"
#include "render/descriptors/vk_bindless_resources_combined.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/descriptors/vk_bindless_resources_storage.h"
#include "render/model/will_model_asset.h"


namespace Render
{
struct ModelEntry
{};

using ModelEntryHandle = Core::Handle<ModelEntry>;

struct InstanceEntry
{};

using InstanceEntryHandle = Core::Handle<InstanceEntry>;

struct MaterialEntry
{};

using MaterialEntryHandle = Core::Handle<MaterialEntry>;

using WillModelHandle = Core::Handle<WillModel>;

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

    // Managed by Engine (asset manager)
    Core::HandleAllocator<ModelEntry, BINDLESS_MODEL_BUFFER_COUNT> modelEntryAllocator;
    Core::HandleAllocator<InstanceEntry, BINDLESS_INSTANCE_BUFFER_COUNT> instanceEntryAllocator;
    Core::HandleAllocator<MaterialEntry, BINDLESS_MATERIAL_BUFFER_COUNT> materialEntryAllocator;
    OffsetAllocator::Allocator jointMatrixAllocator{BINDLESS_MODEL_BUFFER_SIZE};

    std::unordered_map<std::filesystem::path, WillModelHandle> pathToHandle;
    Core::FreeList<WillModel, MAX_LOADED_MODELS> models;

private:
    VulkanContext* context{};
};
} // Render

#endif //WILL_ENGINE_VK_RENDER_RESOURCES_H
