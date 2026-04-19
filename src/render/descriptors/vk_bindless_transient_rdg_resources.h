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
template<size_t SamplerCount, size_t CompareSamplerCount, size_t SampledImageCount,
    size_t StorageFloat4Count, size_t StorageFloat2Count, size_t StorageFloatCount, size_t StorageUInt4Count, size_t StorageUInt2Count, size_t StorageUIntCount,
    size_t SampledFloat2Count, size_t SampledFloatCount, size_t SampledUInt4Count, size_t SampledUInt2Count, size_t SampledUIntCount>
class BindlessTransientRDGResourcesDescriptorBuffer
{
public:
    DescriptorSetLayout descriptorSetLayout{};

    uint32_t GetSamplerCount() { return SamplerCount; }
    uint32_t GetCompareSamplerCount() { return CompareSamplerCount; }
    uint32_t GetSampledImageCount() { return SampledImageCount; }
    uint32_t GetStorageFloat4Count() { return StorageFloat4Count; }
    uint32_t GetStorageFloat2Count() { return StorageFloat2Count; }
    uint32_t GetStorageFloatCount() { return StorageFloatCount; }
    uint32_t GetStorageUInt4Count() { return StorageUInt4Count; }
    uint32_t GetStorageUIntCount() { return StorageUIntCount; }
    uint32_t GetSampledFloat2Count() { return SampledFloat2Count; }
    uint32_t GetSampledFloatCount() { return SampledFloatCount; }
    uint32_t GetSampledUInt4Count() { return SampledUInt4Count; }
    uint32_t GetSampledUInt2Count() { return SampledUInt2Count; }
    uint32_t GetSampledUIntCount() { return SampledUIntCount; }

public:
    BindlessTransientRDGResourcesDescriptorBuffer() = default;

    explicit BindlessTransientRDGResourcesDescriptorBuffer(VulkanContext* context)
        : context(context)
    {
        DescriptorLayoutBuilder layoutBuilder;
        layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, SamplerCount);
        layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLER, CompareSamplerCount);
        layoutBuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SampledImageCount);
        layoutBuilder.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, StorageFloat4Count);
        layoutBuilder.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, StorageFloat2Count);
        layoutBuilder.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, StorageFloatCount);
        layoutBuilder.AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, StorageUInt4Count);
        layoutBuilder.AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, StorageUInt2Count);
        layoutBuilder.AddBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, StorageUIntCount);
        layoutBuilder.AddBinding(9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SampledFloat2Count);
        layoutBuilder.AddBinding(10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SampledFloatCount);
        layoutBuilder.AddBinding(11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SampledUInt4Count);
        layoutBuilder.AddBinding(12, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SampledUInt2Count);
        layoutBuilder.AddBinding(13, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SampledUIntCount);

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
    }

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
        return WriteDescriptorHelper(0, index, VK_DESCRIPTOR_TYPE_SAMPLER,
                                     VulkanContext::deviceInfo.descriptorBufferProps.samplerDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampler = &imageInfo.sampler; });
    }

    bool WriteCompareSamplerDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= CompareSamplerCount) {
            SPDLOG_ERROR("Invalid compare sampler index: {}", index);
            return false;
        }
        return WriteDescriptorHelper(1, index, VK_DESCRIPTOR_TYPE_SAMPLER,
                                     VulkanContext::deviceInfo.descriptorBufferProps.samplerDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampler = &imageInfo.sampler; });
    }

    bool WriteSampledImageDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SampledImageCount) {
            SPDLOG_ERROR("Invalid sampled image index: {}", index);
            return false;
        }
        return WriteDescriptorHelper(2, index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampledImage = &imageInfo; });
    }

    bool WriteStorageFloat4Descriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= StorageFloat4Count) {
            SPDLOG_ERROR("Invalid storage float4 index: {}", index);
            return false;
        }
        return WriteStorageImageHelper(3, index, imageInfo);
    }

    bool WriteStorageFloat2Descriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= StorageFloat2Count) {
            SPDLOG_ERROR("Invalid storage float2 index: {}", index);
            return false;
        }
        return WriteStorageImageHelper(4, index, imageInfo);
    }

    bool WriteStorageFloatDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= StorageFloatCount) {
            SPDLOG_ERROR("Invalid storage float index: {}", index);
            return false;
        }
        return WriteStorageImageHelper(5, index, imageInfo);
    }

    bool WriteStorageUInt4Descriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= StorageUInt4Count) {
            SPDLOG_ERROR("Invalid storage uint4 index: {}", index);
            return false;
        }
        return WriteStorageImageHelper(6, index, imageInfo);
    }

    bool WriteStorageUInt2Descriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= StorageUInt2Count) {
            SPDLOG_ERROR("Invalid storage uint2 index: {}", index);
            return false;
        }
        return WriteStorageImageHelper(7, index, imageInfo);
    }

    bool WriteStorageUIntDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= StorageUIntCount) {
            SPDLOG_ERROR("Invalid storage uint index: {}", index);
            return false;
        }
        return WriteStorageImageHelper(8, index, imageInfo);
    }

    bool WriteSampledFloat2Descriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SampledFloat2Count) {
            SPDLOG_ERROR("Invalid sampled float2 index: {}", index);
            return false;
        }
        return WriteDescriptorHelper(9, index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampledImage = &imageInfo; });
    }

    bool WriteSampledFloatDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SampledFloatCount) {
            SPDLOG_ERROR("Invalid sampled float index: {}", index);
            return false;
        }
        return WriteDescriptorHelper(10, index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampledImage = &imageInfo; });
    }

    bool WriteSampledUInt4Descriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SampledUInt4Count) {
            SPDLOG_ERROR("Invalid sampled uint4 index: {}", index);
            return false;
        }
        return WriteDescriptorHelper(11, index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampledImage = &imageInfo; });
    }

    bool WriteSampledUInt2Descriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SampledUInt2Count) {
            SPDLOG_ERROR("Invalid sampled uint2 index: {}", index);
            return false;
        }
        return WriteDescriptorHelper(12, index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampledImage = &imageInfo; });
    }

    bool WriteSampledUIntDescriptor(uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        if (index >= SampledUIntCount) {
            SPDLOG_ERROR("Invalid sampled uint index: {}", index);
            return false;
        }
        return WriteDescriptorHelper(13, index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     VulkanContext::deviceInfo.descriptorBufferProps.sampledImageDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pSampledImage = &imageInfo; });
    }

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
    AllocatedBuffer buffer{};
    VkDeviceSize descriptorSetSize{};

    template<typename ConfigFunc>
    bool WriteDescriptorHelper(uint32_t binding, uint32_t index, VkDescriptorType type,
                               size_t descriptorSize, ConfigFunc&& configFunc)
    {
        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, binding, &bindingOffset);
        char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

        VkDescriptorGetInfoEXT descriptorGetInfo{};
        descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        descriptorGetInfo.type = type;
        configFunc(descriptorGetInfo);

        char* bufferPtr = basePtr + index * descriptorSize;
        vkGetDescriptorEXT(context->device, &descriptorGetInfo, descriptorSize, bufferPtr);
        return true;
    }

    bool WriteStorageImageHelper(uint32_t binding, uint32_t index, const VkDescriptorImageInfo& imageInfo)
    {
        return WriteDescriptorHelper(binding, index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                     VulkanContext::deviceInfo.descriptorBufferProps.storageImageDescriptorSize,
                                     [&](VkDescriptorGetInfoEXT& info) { info.data.pStorageImage = &imageInfo; });
    }
};
} // Render

#endif //WILL_ENGINE_VK_BINDLESS_TRANSIENT_RDG_RESOURCES_H
