//
// Created by William on 2025-02-11.
//

#include "environment_map_generate_resources.h"

#include "render/descriptors/vk_descriptors.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Editor
{

EnvironmentMapGenerateResources::EnvironmentMapGenerateResources() = default;

EnvironmentMapGenerateResources::EnvironmentMapGenerateResources(Render::VulkanContext* context)
    : context(context)
{
    Render::DescriptorLayoutBuilder layoutBuilder{4};
    layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SAMPLERS);
    layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_TEXTURES_2D);
    layoutBuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_CUBEMAPS);
    layoutBuilder.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_RW_CUBEMAP_ARRAYS);

    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = layoutBuilder.Build(
        VK_SHADER_STAGE_COMPUTE_BIT,
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT
    );
    descriptorSetLayout = Render::DescriptorSetLayout::CreateDescriptorSetLayout(context, layoutCreateInfo);

    vkGetDescriptorSetLayoutSizeEXT(context->device, descriptorSetLayout.handle, &descriptorSetSize);
    descriptorSetSize = Render::VkHelpers::GetAlignedSize(descriptorSetSize,
        Render::VulkanContext::deviceInfo.descriptorBufferProps.descriptorBufferOffsetAlignment);

    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = descriptorSetSize;
    bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    buffer = Render::AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
}

EnvironmentMapGenerateResources::~EnvironmentMapGenerateResources() = default;

EnvironmentMapGenerateResources::EnvironmentMapGenerateResources(EnvironmentMapGenerateResources&& other) noexcept
    : context(other.context)
    , buffer(std::move(other.buffer))
    , descriptorSetLayout(std::move(other.descriptorSetLayout))
    , descriptorSetSize(other.descriptorSetSize)
{
    other.context = nullptr;
}

EnvironmentMapGenerateResources& EnvironmentMapGenerateResources::operator=(EnvironmentMapGenerateResources&& other) noexcept
{
    if (this != &other) {
        context = other.context;
        buffer = std::move(other.buffer);
        descriptorSetLayout = std::move(other.descriptorSetLayout);
        descriptorSetSize = other.descriptorSetSize;
        other.context = nullptr;
    }
    return *this;
}

bool EnvironmentMapGenerateResources::SetSampler(VkSampler sampler, uint32_t index) const
{
    if (index >= MAX_SAMPLERS) {
        SPDLOG_ERROR("Sampler index {} out of range", index);
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorGetInfo.data.pSampler = &sampler;

    const size_t samplerDescriptorSize = Render::VulkanContext::deviceInfo.descriptorBufferProps.samplerDescriptorSize;
    char* bufferPtr = basePtr + index * samplerDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, samplerDescriptorSize, bufferPtr);

    return true;
}

bool EnvironmentMapGenerateResources::SetTexture2D(const VkDescriptorImageInfo& imageInfo, uint32_t index) const
{
    if (index >= MAX_TEXTURES_2D) {
        SPDLOG_ERROR("Texture2D index {} out of range", index);
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 1, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptorGetInfo.data.pSampledImage = &imageInfo;

    const size_t sampledImageDescriptorSize = Render::VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize;
    char* bufferPtr = basePtr + index * sampledImageDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, sampledImageDescriptorSize, bufferPtr);

    return true;
}

bool EnvironmentMapGenerateResources::SetCubemap(const VkDescriptorImageInfo& imageInfo, uint32_t index) const
{
    if (index >= MAX_CUBEMAPS) {
        SPDLOG_ERROR("Cubemap index {} out of range", index);
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 2, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptorGetInfo.data.pSampledImage = &imageInfo;

    const size_t sampledImageDescriptorSize = Render::VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize;
    char* bufferPtr = basePtr + index * sampledImageDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, sampledImageDescriptorSize, bufferPtr);

    return true;
}

bool EnvironmentMapGenerateResources::SetRWCubemapArray(const VkDescriptorImageInfo& imageInfo, uint32_t index) const
{
    if (index >= MAX_RW_CUBEMAP_ARRAYS) {
        SPDLOG_ERROR("RW Cubemap Array index {} out of range", index);
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 3, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorGetInfo.data.pStorageImage = &imageInfo;

    const size_t storageImageDescriptorSize = Render::VulkanContext::deviceInfo.descriptorBufferProps.storageImageDescriptorSize;
    char* bufferPtr = basePtr + index * storageImageDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, storageImageDescriptorSize, bufferPtr);

    return true;
}

VkDescriptorBufferBindingInfoEXT EnvironmentMapGenerateResources::GetBindingInfo() const
{
    VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo{};
    descriptorBufferBindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    descriptorBufferBindingInfo.address = buffer.address;
    descriptorBufferBindingInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    return descriptorBufferBindingInfo;
}

} // Editor