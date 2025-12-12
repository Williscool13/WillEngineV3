//
// Created by William on 2025-12-12.
//

#include "vk_bindless_resources_combined.h"
#include "vk_descriptors.h"
#include "render/render_constants.h"
#include "render/vk_context.h"
#include "render/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
BindlessResourcesCombined::BindlessResourcesCombined() = default;

BindlessResourcesCombined::BindlessResourcesCombined(VulkanContext* context)
    : context(context)
{
    DescriptorLayoutBuilder layoutBuilder{1};
    layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BINDLESS_COMBINED_IMAGE_SAMPLER_COUNT);

    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = layoutBuilder.Build(
        static_cast<VkShaderStageFlagBits>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT),
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT
    );
    descriptorSetLayout = DescriptorSetLayout::CreateDescriptorSetLayout(context, layoutCreateInfo);

    vkGetDescriptorSetLayoutSizeEXT(context->device, descriptorSetLayout.handle, &descriptorSetSize);
    descriptorSetSize = VkHelpers::GetAlignedSize(descriptorSetSize, VulkanContext::deviceInfo.descriptorBufferProps.descriptorBufferOffsetAlignment);

    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = descriptorSetSize;
    bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    buffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
}

BindlessResourcesCombined::~BindlessResourcesCombined() = default;

BindlessResourcesCombined::BindlessResourcesCombined(BindlessResourcesCombined&& other) noexcept
    : context(other.context)
      , buffer(std::move(other.buffer))
      , descriptorSetLayout(std::move(other.descriptorSetLayout))
      , descriptorSetSize(other.descriptorSetSize)
      , combinedAllocator(std::move(other.combinedAllocator))
{
    other.context = nullptr;
}

BindlessResourcesCombined& BindlessResourcesCombined::operator=(BindlessResourcesCombined&& other) noexcept
{
    if (this != &other) {
        context = other.context;
        buffer = std::move(other.buffer);
        descriptorSetLayout = std::move(other.descriptorSetLayout);
        descriptorSetSize = other.descriptorSetSize;
        combinedAllocator = std::move(other.combinedAllocator);

        other.context = nullptr;
    }
    return *this;
}

BindlessCombinedHandle BindlessResourcesCombined::AllocateCombined(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
{
    BindlessCombinedHandle handle = combinedAllocator.Add();
    if (!handle.IsValid()) {
        SPDLOG_WARN("No more combined image sampler indices available");
        return BindlessCombinedHandle::Invalid;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = imageLayout;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorGetInfo.data.pCombinedImageSampler = &imageInfo;

    const size_t combinedDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.combinedImageSamplerDescriptorSize;
    char* bufferPtr = basePtr + handle.index * combinedDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, combinedDescriptorSize, bufferPtr);

    return handle;
}

bool BindlessResourcesCombined::ForceAllocateCombined(BindlessCombinedHandle handle, VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
{
    if (!combinedAllocator.IsValid(handle)) {
        SPDLOG_ERROR("Invalid combined handle for ForceAllocateCombined");
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = imageLayout;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorGetInfo.data.pCombinedImageSampler = &imageInfo;

    const size_t combinedDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.combinedImageSamplerDescriptorSize;
    char* bufferPtr = basePtr + handle.index * combinedDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, combinedDescriptorSize, bufferPtr);

    return true;
}

bool BindlessResourcesCombined::ReleaseCombinedBinding(BindlessCombinedHandle handle)
{
    return combinedAllocator.Remove(handle);
}

VkDescriptorBufferBindingInfoEXT BindlessResourcesCombined::GetBindingInfo() const
{
    VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo{};
    descriptorBufferBindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    descriptorBufferBindingInfo.address = buffer.address;
    descriptorBufferBindingInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    return descriptorBufferBindingInfo;
}
} // Render
