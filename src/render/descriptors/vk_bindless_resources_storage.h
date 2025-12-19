//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_BINDLESS_RESOURCES_STORAGE_H
#define WILL_ENGINE_VK_BINDLESS_RESOURCES_STORAGE_H

#include "vk_descriptors.h"
#include "render/vulkan/vk_resources.h"
#include "core/allocators/handle_allocator.h"
#include "render/render_config.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
struct VulkanContext;

// Phantom type for storage images
struct BindlessStorageImage
{};

/// Type alias for storage image handles
using BindlessStorageImageHandle = Core::Handle<BindlessStorageImage>;

/**
 * Bindless descriptor buffer for storage images.
 *
 * Contains one binding:
 *   - Binding 0: Array of storage images (BINDLESS_STORAGE_IMAGE_COUNT)
 *
 * Uses Vulkan descriptor buffers (VK_EXT_descriptor_buffer) for bindless access.
 * Handles are managed via HandleAllocator and returned on allocation for shader indexing.
 */
template<size_t Count>
struct BindlessResourcesStorage
{
public:
    /// Descriptor set layout defining the bindless resource structure
    DescriptorSetLayout descriptorSetLayout{};

public:
    BindlessResourcesStorage() = default;

    explicit BindlessResourcesStorage(VulkanContext* context)
        : context(context)
    {
        DescriptorLayoutBuilder layoutBuilder{1};
        layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, BINDLESS_STORAGE_IMAGE_COUNT);

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

    ~BindlessResourcesStorage() = default;

    BindlessResourcesStorage(const BindlessResourcesStorage&) = delete;

    BindlessResourcesStorage& operator=(const BindlessResourcesStorage&) = delete;

    BindlessResourcesStorage(BindlessResourcesStorage&& other) noexcept : descriptorSetLayout(std::move(other.descriptorSetLayout)), context(other.context), buffer(std::move(other.buffer)),
                                                                          descriptorSetSize(other.descriptorSetSize), storageImageAllocator(std::move(other.storageImageAllocator))
    {
        other.context = nullptr;
    }

    BindlessResourcesStorage& operator=(BindlessResourcesStorage&& other) noexcept
    {
        if (this != &other) {
            context = other.context;
            buffer = std::move(other.buffer);
            descriptorSetLayout = std::move(other.descriptorSetLayout);
            descriptorSetSize = other.descriptorSetSize;
            storageImageAllocator = std::move(other.storageImageAllocator);

            other.context = nullptr;
        }
        return *this;
    }

    /**
     * Allocate a storage image in the bindless array.
     * @param imageInfo Descriptor info for the storage image
     * @return Handle for the storage image, or Invalid if full
     */
    BindlessStorageImageHandle AllocateStorageImage(const VkDescriptorImageInfo& imageInfo)
    {
        BindlessStorageImageHandle handle = storageImageAllocator.Add();
        if (!handle.IsValid()) {
            SPDLOG_WARN("No more storage image indices available");
            return BindlessStorageImageHandle::INVALID;
        }

        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
        char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

        VkDescriptorGetInfoEXT descriptorGetInfo{};
        descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorGetInfo.data.pStorageImage = &imageInfo;

        const size_t storageImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.storageImageDescriptorSize;
        char* bufferPtr = basePtr + handle.index * storageImageDescriptorSize;
        vkGetDescriptorEXT(context->device, &descriptorGetInfo, storageImageDescriptorSize, bufferPtr);

        return handle;
    }

    /**
     * Force-update a storage image at a specific handle, bypassing allocation tracking.
     * WARNING: Only use for debugging or replacing existing allocations.
     *
     * @param handle Target handle in the storage image array
     * @param imageInfo New descriptor info
     * @return true if handle was valid and updated
     */
    bool ForceAllocateStorageImage(BindlessStorageImageHandle handle, const VkDescriptorImageInfo& imageInfo)
    {
        if (!storageImageAllocator.IsValid(handle)) {
            SPDLOG_ERROR("Invalid storage image handle for ForceAllocateStorageImage");
            return false;
        }

        size_t bindingOffset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(context->device, descriptorSetLayout.handle, 0, &bindingOffset);
        char* basePtr = static_cast<char*>(buffer.allocationInfo.pMappedData) + bindingOffset;

        VkDescriptorGetInfoEXT descriptorGetInfo{};
        descriptorGetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        descriptorGetInfo.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorGetInfo.data.pStorageImage = &imageInfo;

        const size_t storageImageDescriptorSize = VulkanContext::deviceInfo.descriptorBufferProps.storageImageDescriptorSize;
        char* bufferPtr = basePtr + handle.index * storageImageDescriptorSize;
        vkGetDescriptorEXT(context->device, &descriptorGetInfo, storageImageDescriptorSize, bufferPtr);

        return true;
    }

    /**
     * Release a storage image binding, returning it to the free pool.
     * @param handle Handle returned from AllocateStorageImage
     * @return true if successfully released
     */
    bool ReleaseStorageImageBinding(BindlessStorageImageHandle handle)
    {
        return storageImageAllocator.Remove(handle);
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

    Core::HandleAllocator<BindlessStorageImage, Count> storageImageAllocator;
};
} // Render

#endif //WILL_ENGINE_VK_BINDLESS_RESOURCES_STORAGE_H
