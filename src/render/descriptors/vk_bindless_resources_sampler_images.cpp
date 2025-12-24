//
// Created by William on 2025-12-12.
//

#include "vk_bindless_resources_sampler_images.h"

#include "vk_descriptors.h"
#include "render/render_config.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
BindlessResourcesSamplerImages::BindlessResourcesSamplerImages() = default;

BindlessResourcesSamplerImages::BindlessResourcesSamplerImages(VulkanContext* context)
    : context(context)
{
    DescriptorLayoutBuilder layoutBuilder{2};
    layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, BINDLESS_SAMPLER_COUNT);
    layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, BINDLESS_SAMPLED_IMAGE_COUNT);

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

BindlessResourcesSamplerImages::~BindlessResourcesSamplerImages() = default;

BindlessResourcesSamplerImages::BindlessResourcesSamplerImages(BindlessResourcesSamplerImages&& other) noexcept
    : context(other.context)
      , buffer(std::move(other.buffer))
      , descriptorSetLayout(std::move(other.descriptorSetLayout))
      , descriptorSetSize(other.descriptorSetSize)
      , samplerAllocator(std::move(other.samplerAllocator))
      , textureAllocator(std::move(other.textureAllocator))
{
    other.context = nullptr;
}

BindlessResourcesSamplerImages& BindlessResourcesSamplerImages::operator=(BindlessResourcesSamplerImages&& other) noexcept
{
    if (this != &other) {
        context = other.context;
        buffer = std::move(other.buffer);
        descriptorSetLayout = std::move(other.descriptorSetLayout);
        descriptorSetSize = other.descriptorSetSize;
        samplerAllocator = std::move(other.samplerAllocator);
        textureAllocator = std::move(other.textureAllocator);

        other.context = nullptr;
    }
    return *this;
}

BindlessSamplerHandle BindlessResourcesSamplerImages::AllocateSampler(VkSampler sampler)
{
    BindlessSamplerHandle handle = samplerAllocator.Add();
    if (!handle.IsValid()) {
        SPDLOG_WARN("No more sampler indices available");
        return BindlessSamplerHandle::INVALID;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorGetInfo.data.pSampler = &sampler;

    const size_t samplerDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.samplerDescriptorSize;
    char* bufferPtr = basePtr + handle.index * samplerDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, samplerDescriptorSize, bufferPtr);

    return handle;
}

BindlessTextureHandle BindlessResourcesSamplerImages::AllocateTexture(const VkDescriptorImageInfo& imageInfo)
{
    BindlessTextureHandle handle = textureAllocator.Add();
    if (!handle.IsValid()) {
        SPDLOG_WARN("No more texture indices available");
        return BindlessTextureHandle::INVALID;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 1, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptorGetInfo.data.pSampledImage = &imageInfo;

    const size_t sampledImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize;
    char* bufferPtr = basePtr + handle.index * sampledImageDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, sampledImageDescriptorSize, bufferPtr);

    return handle;
}

BindlessTextureHandle BindlessResourcesSamplerImages::ReserveAllocateTexture()
{
    BindlessTextureHandle handle = textureAllocator.Add();
    if (!handle.IsValid()) {
        SPDLOG_WARN("No more texture indices available");
        return BindlessTextureHandle::INVALID;
    }

    return handle;
}

bool BindlessResourcesSamplerImages::ForceAllocateTexture(BindlessTextureHandle handle, const VkDescriptorImageInfo& imageInfo)
{
    if (!textureAllocator.IsValid(handle)) {
        SPDLOG_ERROR("Invalid texture handle for ForceAllocateTexture");
        return false;
    }

    size_t bindingOffset;
    vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 1, &bindingOffset);
    char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

    VkDescriptorGetInfoEXT descriptorGetInfo{};
    descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptorGetInfo.data.pSampledImage = &imageInfo;

    const size_t sampledImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize;
    char* bufferPtr = basePtr + handle.index * sampledImageDescriptorSize;
    vkGetDescriptorEXT(context->device, &descriptorGetInfo, sampledImageDescriptorSize, bufferPtr);

    return true;
}

bool BindlessResourcesSamplerImages::ReleaseSamplerBinding(BindlessSamplerHandle handle)
{
    return samplerAllocator.Remove(handle);
}

bool BindlessResourcesSamplerImages::ReleaseTextureBinding(BindlessTextureHandle handle)
{
    return textureAllocator.Remove(handle);
}

VkDescriptorBufferBindingInfoEXT BindlessResourcesSamplerImages::GetBindingInfo() const
{
    VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo{};
    descriptorBufferBindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    descriptorBufferBindingInfo.address = buffer.address;
    descriptorBufferBindingInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    return descriptorBufferBindingInfo;
}
} // Render
