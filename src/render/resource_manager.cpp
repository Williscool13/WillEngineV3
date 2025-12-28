//
// Created by William on 2025-12-11.
//

#include "resource_manager.h"

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
    megaVertexBuffer.SetDebugName("Mega Vertex Buffer");
    bufferInfo.size = MEGA_MESHLET_VERTEX_BUFFER_SIZE;
    megaMeshletVerticesBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    megaMeshletVerticesBuffer.SetDebugName("Mega Meshlet Vertex Buffer");
    bufferInfo.size = MEGA_MESHLET_TRIANGLE_BUFFER_SIZE;
    megaMeshletTrianglesBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    megaMeshletTrianglesBuffer.SetDebugName("Mega Meshlet Triangle Buffer");
    bufferInfo.size = MEGA_MESHLET_BUFFER_SIZE;
    megaMeshletBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    megaMeshletBuffer.SetDebugName("Mega Meshlet Buffer");
    bufferInfo.size = MEGA_PRIMITIVE_BUFFER_SIZE;
    primitiveBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    primitiveBuffer.SetDebugName("Mega Primitive Buffer");

    bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (int32_t i = 0; i < frameResources.size(); ++i) {
        bufferInfo.size = sizeof(SceneData);
        frameResources[i].sceneDataBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));

        bufferInfo.size = BINDLESS_INSTANCE_BUFFER_SIZE;
        frameResources[i].instanceBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        frameResources[i].instanceBuffer.SetDebugName(fmt::format("Instance Buffer {}", i).c_str());
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResources[i].modelBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        frameResources[i].modelBuffer.SetDebugName(fmt::format("Model Buffer {}", i).c_str());
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResources[i].jointMatrixBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        frameResources[i].jointMatrixBuffer.SetDebugName(fmt::format("Joint Matrix Buffer {}", i).c_str());
        bufferInfo.size = BINDLESS_MATERIAL_BUFFER_SIZE;
        frameResources[i].materialBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        frameResources[i].materialBuffer.SetDebugName(fmt::format("Material Buffer {}", i).c_str());
    }

    bindlessSamplerTextureDescriptorBuffer = BindlessResourcesSamplerImages(context);
    bindlessRDGTransientDescriptorBuffer = BindlessTransientRDGResourcesDescriptorBuffer<8, 128, 128>(context);
}
} // Render
