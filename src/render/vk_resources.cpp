//
// Created by William on 2025-12-11.
//

#include "vk_resources.h"

#include "vk_context.h"
#include "vk_helpers.h"
#include "vk_utils.h"

namespace Render
{
AllocatedBuffer::~AllocatedBuffer()
{
    if (handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context->allocator, handle, allocation);
        handle = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        address = 0;
        size = 0;
    }
}

AllocatedBuffer::AllocatedBuffer(AllocatedBuffer&& other) noexcept
{
    context = other.context;
    handle = other.handle;
    address = other.address;
    size = other.size;
    allocation = other.allocation;
    allocationInfo = other.allocationInfo;

    other.context = nullptr;
    other.handle = VK_NULL_HANDLE;
    other.address = 0;
    other.size = 0;
    other.allocation = VK_NULL_HANDLE;
    other.allocationInfo = {};
}

AllocatedBuffer& AllocatedBuffer::operator=(AllocatedBuffer&& other) noexcept
{
    if (this != &other) {
        if (handle != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context->allocator, handle, allocation);
            handle = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
            address = 0;
            size = 0;
        }

        context = other.context;
        handle = other.handle;
        address = other.address;
        size = other.size;
        allocation = other.allocation;
        allocationInfo = other.allocationInfo;

        other.context = nullptr;
        other.handle = VK_NULL_HANDLE;
        other.address = 0;
        other.size = 0;
        other.allocation = VK_NULL_HANDLE;
        other.allocationInfo = {};
    }
    return *this;
}

void AllocatedBuffer::Release()
{
    if (handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context->allocator, handle, allocation);
        handle = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        address = 0;
        size = 0;
    }
}

AllocatedImage::~AllocatedImage()
{
    if (handle != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(context->allocator, handle, allocation);
        handle = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
    extent = {};
    format = VK_FORMAT_UNDEFINED;
    layout = VK_IMAGE_LAYOUT_UNDEFINED;
    mipLevels = 0;
}

AllocatedImage::AllocatedImage(AllocatedImage&& other) noexcept
{
    context = other.context;
    handle = other.handle;
    format = other.format;
    extent = other.extent;
    layout = other.layout;
    mipLevels = other.mipLevels;
    allocation = other.allocation;

    other.context = nullptr;
    other.handle = VK_NULL_HANDLE;
    other.format = VK_FORMAT_UNDEFINED;
    other.extent = {};
    other.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    other.mipLevels = 0;
    other.allocation = {};
}

AllocatedImage& AllocatedImage::operator=(AllocatedImage&& other) noexcept
{
    if (this != &other) {
        if (handle != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
            vmaDestroyImage(context->allocator, handle, allocation);
            handle = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
        }
        extent = {};
        format = VK_FORMAT_UNDEFINED;
        layout = VK_IMAGE_LAYOUT_UNDEFINED;
        mipLevels = 0;

        context = other.context;
        handle = other.handle;
        format = other.format;
        extent = other.extent;
        layout = other.layout;
        mipLevels = other.mipLevels;
        allocation = other.allocation;

        other.context = nullptr;
        other.handle = VK_NULL_HANDLE;
        other.format = VK_FORMAT_UNDEFINED;
        other.extent = {};
        other.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        other.mipLevels = 0;
        other.allocation = {};
    }
    return *this;
}

ImageView::~ImageView()
{
    if (handle != VK_NULL_HANDLE) {
        vkDestroyImageView(context->device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}

ImageView::ImageView(ImageView&& other) noexcept
{
    context = other.context;
    handle = other.handle;

    other.context = nullptr;
    other.handle = VK_NULL_HANDLE;
}

ImageView& ImageView::operator=(ImageView&& other) noexcept
{
    if (this != &other) {
        if (handle != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, handle, nullptr);
            handle = VK_NULL_HANDLE;
        }

        context = other.context;
        handle = other.handle;

        other.context = nullptr;
        other.handle = VK_NULL_HANDLE;
    }
    return *this;
}

Sampler::~Sampler()
{
    if (handle != VK_NULL_HANDLE) {
        vkDestroySampler(context->device, handle, nullptr);
    }
}

Sampler::Sampler(Sampler&& other) noexcept
{
    context = other.context;
    handle = other.handle;

    other.context = nullptr;
    other.handle = VK_NULL_HANDLE;
}

Sampler& Sampler::operator=(Sampler&& other) noexcept
{
    if (this != &other) {
        if (handle != VK_NULL_HANDLE) {
            vkDestroySampler(context->device, handle, nullptr);
        }

        context = other.context;
        handle = other.handle;

        other.context = nullptr;
        other.handle = VK_NULL_HANDLE;
    }
    return *this;
}

DescriptorSetLayout::~DescriptorSetLayout()
{
    if (context && handle != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context->device, handle, nullptr);
    }
}

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
{
    context = other.context;
    handle = other.handle;

    other.context = nullptr;
    other.handle = VK_NULL_HANDLE;
}

DescriptorSetLayout& DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept
{
    if (this != &other) {
        if (context && handle != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(context->device, handle, nullptr);
        }

        context = other.context;
        handle = other.handle;

        other.context = nullptr;
        other.handle = VK_NULL_HANDLE;
    }
    return *this;
}

PipelineLayout::~PipelineLayout()
{
    if (context && handle != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context->device, handle, nullptr);
    }
}

PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept
{
    context = other.context;
    handle = other.handle;

    other.context = nullptr;
    other.handle = VK_NULL_HANDLE;
}

PipelineLayout& PipelineLayout::operator=(PipelineLayout&& other) noexcept
{
    if (this != &other) {
        if (context && handle != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context->device, handle, nullptr);
        }

        context = other.context;
        handle = other.handle;

        other.context = nullptr;
        other.handle = VK_NULL_HANDLE;
    }
    return *this;
}

Pipeline::~Pipeline()
{
    if (context && handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(context->device, handle, nullptr);
    }
}

Pipeline::Pipeline(Pipeline&& other) noexcept
{
    context = other.context;
    handle = other.handle;

    other.context = nullptr;
    other.handle = VK_NULL_HANDLE;
}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept
{
    if (this != &other) {
        if (context && handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(context->device, handle, nullptr);
        }

        context = other.context;
        handle = other.handle;

        other.context = nullptr;
        other.handle = VK_NULL_HANDLE;
    }
    return *this;
}

AllocatedBuffer AllocatedBuffer::CreateAllocatedBuffer(const VulkanContext* context, const VkBufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& vmaAllocInfo)
{
    AllocatedBuffer buffer;
    buffer.context = context;
    VK_CHECK(vmaCreateBuffer(context->allocator, &bufferInfo, &vmaAllocInfo, &buffer.handle, &buffer.allocation, &buffer.allocationInfo));
    buffer.size = bufferInfo.size;
    if (bufferInfo.usage & VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT) {
        buffer.address = VkHelpers::GetDeviceAddress(context->device, buffer.handle);
    }
    return buffer;
}
AllocatedBuffer AllocatedBuffer::CreateAllocatedStagingBuffer(const VulkanContext* context, size_t bufferSize, VkBufferUsageFlags additionalUsages)
{
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | additionalUsages,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    constexpr VmaAllocationCreateInfo allocInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = 0
    };

    AllocatedBuffer buffer;
    buffer.context = context;
    VK_CHECK(vmaCreateBuffer(context->allocator, &bufferInfo, &allocInfo, &buffer.handle, &buffer.allocation, &buffer.allocationInfo));
    buffer.size = bufferInfo.size;
    // Staging buffer doesn't typically need device address. If needed, make a new function
    // buffer.address = VkHelpers::GetDeviceAddress(context->device, buffer.handle);
    return buffer;
}
AllocatedBuffer AllocatedBuffer::CreateAllocatedReceivingBuffer(const VulkanContext* context, size_t bufferSize, VkBufferUsageFlags additionalUsages)
{
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | additionalUsages,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    constexpr VmaAllocationCreateInfo allocInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = 0
    };

    AllocatedBuffer buffer;
    buffer.context = context;
    VK_CHECK(vmaCreateBuffer(context->allocator, &bufferInfo, &allocInfo, &buffer.handle, &buffer.allocation, &buffer.allocationInfo));
    buffer.size = bufferInfo.size;
    buffer.address = VkHelpers::GetDeviceAddress(context->device, buffer.handle);
    return buffer;
}

AllocatedImage AllocatedImage::CreateAllocatedImage(const VulkanContext* context, const VkImageCreateInfo& imageCreateInfo)
{
    AllocatedImage newImage;
    newImage.context = context;
    constexpr VmaAllocationCreateInfo allocInfo{
        .flags = 0,
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    newImage.format = imageCreateInfo.format;
    newImage.extent = imageCreateInfo.extent;
    newImage.mipLevels = imageCreateInfo.mipLevels;
    VK_CHECK(vmaCreateImage(context->allocator, &imageCreateInfo, &allocInfo, &newImage.handle, &newImage.allocation, nullptr));
    return newImage;
}

ImageView ImageView::CreateImageView(const VulkanContext* context, const VkImageViewCreateInfo& imageViewCreateInfo)
{
    ImageView newImageView;
    newImageView.context = context;
    VK_CHECK(vkCreateImageView(context->device, &imageViewCreateInfo, nullptr, &newImageView.handle));
    return newImageView;
}

Sampler Sampler::CreateSampler(const VulkanContext* context, const VkSamplerCreateInfo& samplerCreateInfo)
{
    Sampler sampler;
    sampler.context = context;
    vkCreateSampler(context->device, &samplerCreateInfo, nullptr, &sampler.handle);
    return sampler;
}

DescriptorSetLayout DescriptorSetLayout::CreateDescriptorSetLayout(const VulkanContext* context, const VkDescriptorSetLayoutCreateInfo& layoutCreateInfo)
{
    DescriptorSetLayout layout;
    layout.context = context;
    VK_CHECK(vkCreateDescriptorSetLayout(context->device, &layoutCreateInfo, nullptr, &layout.handle));
    return layout;
}

PipelineLayout PipelineLayout::CreatePipelineLayout(const VulkanContext* context, const VkPipelineLayoutCreateInfo& layoutCreateInfo)
{
    PipelineLayout layout;
    layout.context = context;
    VK_CHECK(vkCreatePipelineLayout(context->device, &layoutCreateInfo, nullptr, &layout.handle));
    return layout;
}

Pipeline Pipeline::CreateGraphicsPipeline(const VulkanContext* context, const VkGraphicsPipelineCreateInfo& pipelineCreateInfo)
{
    Pipeline pipeline;
    pipeline.context = context;
    VK_CHECK(vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline.handle));
    return pipeline;
}

Pipeline Pipeline::CreateComputePipeline(const VulkanContext* context, const VkComputePipelineCreateInfo& pipelineCreateInfo)
{
    Pipeline pipeline;
    pipeline.context = context;
    VK_CHECK(vkCreateComputePipelines(context->device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline.handle));
    return pipeline;
}
} // Render
