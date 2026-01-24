//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_VK_BINDLESS_TRANSIENT_RDG_RESOURCES_H
#define WILL_ENGINE_VK_BINDLESS_TRANSIENT_RDG_RESOURCES_H
#include "vk_descriptors.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_resources.h"
#include "spdlog/spdlog.h"

namespace Render
{
template<size_t SamplerCount, size_t CompareSamplerCount, size_t SampledImageCount, size_t FloatStorageImageCount>
class BindlessTransientRDGResourcesDescriptorBuffer
{
public:
    /// Descriptor set layout defining the bindless resource structure
    DescriptorSetLayout descriptorSetLayout{};

    uint32_t GetSamplerCount() { return SamplerCount; }
    uint32_t GetCompareSamplerCount() { return CompareSamplerCount; }
    uint32_t GetSampledImageCount() { return SampledImageCount; }
    uint32_t GetStorageImageCount() { return FloatStorageImageCount; }

public:
    BindlessTransientRDGResourcesDescriptorBuffer() = default;

    explicit BindlessTransientRDGResourcesDescriptorBuffer(VulkanContext* context)
        : context(context)
    {
        DescriptorLayoutBuilder layoutBuilder{1};
        layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, CompareSamplerCount);
        layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLER, SamplerCount);
        layoutBuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SampledImageCount);
        layoutBuilder.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FloatStorageImageCount);

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo = layoutBuilder.Build(
            static_cast<VkShaderStageFlagBits>(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
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
    };

    ~BindlessTransientRDGResourcesDescriptorBuffer() = default;

    BindlessTransientRDGResourcesDescriptorBuffer(const BindlessTransientRDGResourcesDescriptorBuffer&) = delete;

    BindlessTransientRDGResourcesDescriptorBuffer& operator=(const BindlessTransientRDGResourcesDescriptorBuffer&) = delete;

    BindlessTransientRDGResourcesDescriptorBuffer(BindlessTransientRDGResourcesDescriptorBuffer&& other) noexcept : descriptorSetLayout(std::move(other.descriptorSetLayout)), context(other.context),
                                                                                                                    buffer(std::move(other.buffer)),
                                                                                                                    descriptorSetSize(other.descriptorSetSize)
    {
        other.context = nullptr;
    }

    BindlessTransientRDGResourcesDescriptorBuffer& operator=(BindlessTransientRDGResourcesDescriptorBuffer&& other) noexcept
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

    bool WriteSamplerDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SamplerCount) {
            SPDLOG_ERROR("Invalid sampler index: {}", index);
            return false;
        }

        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
        char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

        VkDescriptorGetInfoEXT descriptorGetInfo{};
        descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorGetInfo.data.pSampler = &imageInfo.sampler;

        const size_t storageImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.samplerDescriptorSize;
        char* bufferPtr = basePtr + index * storageImageDescriptorSize;
        vkGetDescriptorEXT(context->device, &descriptorGetInfo, storageImageDescriptorSize, bufferPtr);

        return true;
    }
    bool WriteCompareSamplerDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= CompareSamplerCount) {
            SPDLOG_ERROR("Invalid compare sampler index: {}", index);
            return false;
        }

        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 1, &bindingOffset);
        char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

        VkDescriptorGetInfoEXT descriptorGetInfo{};
        descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorGetInfo.data.pSampler = &imageInfo.sampler;

        const size_t storageImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.samplerDescriptorSize;
        char* bufferPtr = basePtr + index * storageImageDescriptorSize;
        vkGetDescriptorEXT(context->device, &descriptorGetInfo, storageImageDescriptorSize, bufferPtr);

        return true;
    }
    bool WriteSampledImageDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SampledImageCount) {
            SPDLOG_ERROR("Invalid sampled image index: {}", index);
            return false;
        }

        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 2, &bindingOffset);
        char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

        VkDescriptorGetInfoEXT descriptorGetInfo{};
        descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descriptorGetInfo.data.pSampledImage = &imageInfo;

        const size_t storageImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize;
        char* bufferPtr = basePtr + index * storageImageDescriptorSize;
        vkGetDescriptorEXT(context->device, &descriptorGetInfo, storageImageDescriptorSize, bufferPtr);

        return true;
    }
    bool WriteStorageImageDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= FloatStorageImageCount) {
            SPDLOG_ERROR("Invalid storage image index {}", index);
            return false;
        }

        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 3, &bindingOffset);
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

    /**
    * Get binding info for vkCmdBindDescriptorBuffersEXT.
    * @return Descriptor buffer binding info
    */
    [[nodiscard]] VkDescriptorBufferBindingInfoEXT GetBindingInfo() const
    {
        VkDescriptorBufferBindingInfoEXT descriptorBufferBindingInfo{};
        descriptorBufferBindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        descriptorBufferBindingInfo.address = buffer.address;
        descriptorBufferBindingInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
        return descriptorBufferBindingInfo;
    }

private:
    VulkanContext* context{};
    AllocatedBuffer buffer{}; ///< GPU buffer containing descriptor data
    VkDeviceSize descriptorSetSize{}; ///< Aligned size of the descriptor set
};
} // Render

#endif //WILL_ENGINE_VK_BINDLESS_TRANSIENT_RDG_RESOURCES_H
