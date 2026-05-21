//
// Created by William on 2026-05-21.
//

#include "procedural_texture_generate_resources.h"

#include "vk_descriptors.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{

ProceduralTextureGenerateResources::ProceduralTextureGenerateResources() = default;

ProceduralTextureGenerateResources::ProceduralTextureGenerateResources(VulkanContext* context)
    : context(context)
{
    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SAMPLERS);
    layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_RW_TEXTURES);

    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = layoutBuilder.Build(
        VK_SHADER_STAGE_COMPUTE_BIT,
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT
    );
    descriptorSetLayout = DescriptorSetLayout::CreateDescriptorSetLayout(context, layoutCreateInfo);

    vkGetDescriptorSetLayoutSizeEXT(context->device, descriptorSetLayout.handle, &descriptorSetSize);
    descriptorSetSize = VkHelpers::GetAlignedSize(descriptorSetSize,
        VulkanContext::deviceInfo.descriptorBufferProps.descriptorBufferOffsetAlignment);

    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = descriptorSetSize;
    bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    buffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
}

ProceduralTextureGenerateResources::~ProceduralTextureGenerateResources() = default;

ProceduralTextureGenerateResources::ProceduralTextureGenerateResources(ProceduralTextureGenerateResources&& other) noexcept
    : descriptorSetLayout(std::move(other.descriptorSetLayout))
    , context(other.context)
    , buffer(std::move(other.buffer))
    , descriptorSetSize(other.descriptorSetSize)
{
    other.context = nullptr;
}

ProceduralTextureGenerateResources& ProceduralTextureGenerateResources::operator=(ProceduralTextureGenerateResources&& other) noexcept
{
    if (this != &other) {
        descriptorSetLayout = std::move(other.descriptorSetLayout);
        context = other.context;
        buffer = std::move(other.buffer);
        descriptorSetSize = other.descriptorSetSize;
        other.context = nullptr;
    }
    return *this;
}

bool ProceduralTextureGenerateResources::SetSampler(VkSampler sampler, uint32_t index) const
{
    if (index >= MAX_SAMPLERS) {
        SPDLOG_ERROR("ProceduralTextureGenerateResources: sampler index {} out of range", index);
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorGetInfo.data.pSampler = &sampler;

    const size_t samplerDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.samplerDescriptorSize;
    char* bufferPtr = basePtr + index * samplerDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, samplerDescriptorSize, bufferPtr);

    return true;
}

bool ProceduralTextureGenerateResources::SetRWTexture(const VkDescriptorImageInfo& imageInfo, uint32_t index) const
{
    if (index >= MAX_RW_TEXTURES) {
        SPDLOG_ERROR("ProceduralTextureGenerateResources: RW texture index {} out of range", index);
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 1, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorGetInfo.data.pStorageImage = &imageInfo;

    const size_t storageImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.storageImageDescriptorSize;
    char* bufferPtr = basePtr + index * storageImageDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, storageImageDescriptorSize, bufferPtr);

    return true;
}

VkDescriptorBufferBindingInfoEXT ProceduralTextureGenerateResources::GetBindingInfo() const
{
    VkDescriptorBufferBindingInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    info.address = buffer.address;
    info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    return info;
}
} // Render
