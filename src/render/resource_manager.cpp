//
// Created by William on 2025-12-11.
//

#include "resource_manager.h"

#include "vulkan/vk_utils.h"

namespace Render
{
ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager()
{
    if (pointSampler != VK_NULL_HANDLE) {
        vkDestroySampler(context->device, pointSampler, nullptr);
    }
    if (linearSampler != VK_NULL_HANDLE) {
        vkDestroySampler(context->device, linearSampler, nullptr);
    }
    if (depthCompareSampler != VK_NULL_HANDLE) {
        vkDestroySampler(context->device, depthCompareSampler, nullptr);
    }
};

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
    bufferInfo.size = MEGA_SKINNED_VERTEX_BUFFER_SIZE;
    megaSkinnedVertexBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    megaSkinnedVertexBuffer.SetDebugName("Mega Skinned Vertex Buffer");
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

    bindlessSamplerTextureDescriptorBuffer = BindlessResourcesSamplerImages(context);
    bindlessRDGTransientDescriptorBuffer = BindlessTransientRDGResourcesDescriptorBuffer<4, 4, RDG_MAX_SAMPLED_TEXTURES, RDG_MAX_STORAGE_TEXTURES>(context);

    bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    bufferInfo.size = 8 * 1024 * 1024;
    debugReadbackBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    debugReadbackBuffer.SetDebugName("Debug Readback Buffer");

    VkSamplerCreateInfo pointSamplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };

    VK_CHECK(vkCreateSampler(context->device, &pointSamplerInfo, nullptr, &pointSampler));
    bindlessRDGTransientDescriptorBuffer.WriteSamplerDescriptor(RDG_POINT_SAMPLER_INDEX, {pointSampler, nullptr, {}});

    VkSamplerCreateInfo linearSamplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };

    VK_CHECK(vkCreateSampler(context->device, &linearSamplerInfo, nullptr, &linearSampler));
    bindlessRDGTransientDescriptorBuffer.WriteSamplerDescriptor(RDG_LINEAR_SAMPLER_INDEX, {linearSampler, nullptr, {}});

    VkSamplerCreateInfo depthCompareSamplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_TRUE,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };

    VK_CHECK(vkCreateSampler(context->device, &depthCompareSamplerInfo, nullptr, &depthCompareSampler));
    bindlessRDGTransientDescriptorBuffer.WriteCompareSamplerDescriptor(RDG_LINEAR_DEPTH_SAMPLER_INDEX, {depthCompareSampler, nullptr, {}});
}
} // Render
