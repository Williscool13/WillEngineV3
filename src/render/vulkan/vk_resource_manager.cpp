//
// Created by William on 2025-12-11.
//

#include "vk_resource_manager.h"

namespace Render
{
ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager() = default;

ResourceManager::ResourceManager(VulkanContext* context)
    : context(context)
{
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    bufferInfo.size = MEGA_VERTEX_BUFFER_SIZE;
    megaVertexBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    bufferInfo.size = MEGA_MESHLET_VERTEX_BUFFER_SIZE;
    megaMeshletVerticesBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    bufferInfo.size = MEGA_MESHLET_TRIANGLE_BUFFER_SIZE;
    megaMeshletTrianglesBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    bufferInfo.size = MEGA_MESHLET_BUFFER_SIZE;
    megaMeshletBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    bufferInfo.size = MEGA_PRIMITIVE_BUFFER_SIZE;
    primitiveBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);

    bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    for (FrameResources& frameResource : frameResources) {
        bufferInfo.size = sizeof(SceneData);
        frameResource.sceneDataBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));

        bufferInfo.size = BINDLESS_INSTANCE_BUFFER_SIZE;
        frameResource.instanceBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResource.modelBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResource.jointMatrixBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MATERIAL_BUFFER_SIZE;
        frameResource.materialBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
    }

    bindlessSamplerTextureDescriptorBuffer = BindlessResourcesSamplerImages(context);
    bindlessRenderTargetDescriptorBuffer = BindlessResourcesStorage<8>(context);
    bindlessStorageDescriptorBuffer = BindlessResourcesStorage<512>(context);
}
} // Render
