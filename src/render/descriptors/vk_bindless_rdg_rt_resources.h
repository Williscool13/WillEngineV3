//
// Created by William on 2026-06-10.
//

#ifndef WILL_ENGINE_VK_BINDLESS_RDG_RT_RESOURCES_H
#define WILL_ENGINE_VK_BINDLESS_RDG_RT_RESOURCES_H

#include "vk_descriptors.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_resources.h"
#include "spdlog/spdlog.h"

namespace Render
{
template<size_t AccelerationStructureCount>
class BindlessRDGRTResourcesDescriptorBuffer
{
public:
    DescriptorSetLayout descriptorSetLayout{};

public:
    BindlessRDGRTResourcesDescriptorBuffer() = default;

    explicit BindlessRDGRTResourcesDescriptorBuffer(VulkanContext* context)
        : context(context)
    {
        DescriptorLayoutBuilder layoutBuilder;
        layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, AccelerationStructureCount);

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo = layoutBuilder.Build(
            VK_SHADER_STAGE_COMPUTE_BIT,
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

    ~BindlessRDGRTResourcesDescriptorBuffer() = default;

    BindlessRDGRTResourcesDescriptorBuffer(const BindlessRDGRTResourcesDescriptorBuffer&) = delete;
    BindlessRDGRTResourcesDescriptorBuffer& operator=(const BindlessRDGRTResourcesDescriptorBuffer&) = delete;

    BindlessRDGRTResourcesDescriptorBuffer(BindlessRDGRTResourcesDescriptorBuffer&& other) noexcept
        : descriptorSetLayout(std::move(other.descriptorSetLayout)),
          context(other.context),
          buffer(std::move(other.buffer)),
          descriptorSetSize(other.descriptorSetSize)
    {
        other.context = nullptr;
    }

    BindlessRDGRTResourcesDescriptorBuffer& operator=(BindlessRDGRTResourcesDescriptorBuffer&& other) noexcept
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

    bool WriteAccelerationStructureDescriptor(uint32_t index, VkDeviceAddress address)
    {
        if (index >= AccelerationStructureCount) {
            SPDLOG_ERROR("Invalid acceleration structure index: {}", index);
            return false;
        }

        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
        char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

        VkDescriptorGetInfoEXT descriptorGetInfo{};
        descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        descriptorGetInfo.data.accelerationStructure = address;

        const size_t descriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.accelerationStructureDescriptorSize;
        vkGetDescriptorEXT(context->device, &descriptorGetInfo, descriptorSize, basePtr + index * descriptorSize);
        return true;
    }

    [[nodiscard]] VkDescriptorBufferBindingInfoEXT GetBindingInfo() const
    {
        VkDescriptorBufferBindingInfoEXT bindingInfo{};
        bindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        bindingInfo.address = buffer.address;
        bindingInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
        return bindingInfo;
    }

private:
    VulkanContext* context{};
    AllocatedBuffer buffer{};
    VkDeviceSize descriptorSetSize{};
};
} // Render

#endif //WILL_ENGINE_VK_BINDLESS_RDG_RT_RESOURCES_H
