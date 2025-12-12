//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_RESOURCES_H
#define WILL_ENGINE_VK_RESOURCES_H

#include <volk.h>
#include <vk_mem_alloc.h>

namespace Render
{
struct VulkanContext;


struct AllocatedBuffer
{
    const VulkanContext* context{nullptr};

    VkBuffer handle{VK_NULL_HANDLE};
    VkDeviceAddress address{0};
    size_t size{0};

    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocationInfo{};

    AllocatedBuffer() = default;

    static AllocatedBuffer CreateAllocatedBuffer(const VulkanContext* context, const VkBufferCreateInfo& bufferInfo, const VmaAllocationCreateInfo& vmaAllocInfo);

    static AllocatedBuffer CreateAllocatedStagingBuffer(const VulkanContext* context, size_t bufferSize, VkBufferUsageFlags additionalUsages = 0);

    static AllocatedBuffer CreateAllocatedReceivingBuffer(const VulkanContext* context, size_t bufferSize, VkBufferUsageFlags additionalUsages = 0);

    ~AllocatedBuffer();

    AllocatedBuffer(const AllocatedBuffer&) = delete;

    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;

    AllocatedBuffer(AllocatedBuffer&& other) noexcept;

    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept;

    /**
     * Explicitly release the buffer's resources. Use carefully.
     */
    void Release();
};

struct AllocatedImage
{
    const VulkanContext* context{nullptr};

    VkImage handle{VK_NULL_HANDLE};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkExtent3D extent{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    uint32_t mipLevels{0};
    VmaAllocation allocation{};

    AllocatedImage() = default;

    static AllocatedImage CreateAllocatedImage(const VulkanContext* context, const VkImageCreateInfo& imageCreateInfo);

    ~AllocatedImage();

    AllocatedImage(const AllocatedImage&) = delete;

    AllocatedImage& operator=(const AllocatedImage&) = delete;

    AllocatedImage(AllocatedImage&& other) noexcept;

    AllocatedImage& operator=(AllocatedImage&& other) noexcept;
};

struct ImageView
{
    const VulkanContext* context{};

    VkImageView handle{};

    ImageView() = default;

    static ImageView CreateImageView(const VulkanContext* context, const VkImageViewCreateInfo& imageViewCreateInfo);

    ~ImageView();

    ImageView(const ImageView&) = delete;

    ImageView& operator=(const ImageView&) = delete;

    ImageView(ImageView&& other) noexcept;

    ImageView& operator=(ImageView&& other) noexcept;
};

struct Sampler
{
    const VulkanContext* context{};

    VkSampler handle{};

    Sampler() = default;

    static Sampler CreateSampler(const VulkanContext* context, const VkSamplerCreateInfo& samplerCreateInfo);

    ~Sampler();

    Sampler(const Sampler&) = delete;

    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&& other) noexcept;

    Sampler& operator=(Sampler&& other) noexcept;
};

struct DescriptorSetLayout
{
    const VulkanContext* context{};
    VkDescriptorSetLayout handle{};

    DescriptorSetLayout() = default;

    static DescriptorSetLayout CreateDescriptorSetLayout(const VulkanContext* context, const VkDescriptorSetLayoutCreateInfo& layoutCreateInfo);

    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;

    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

    DescriptorSetLayout(DescriptorSetLayout&& other) noexcept;

    DescriptorSetLayout& operator=(DescriptorSetLayout&& other) noexcept;
};

struct PipelineLayout
{
    const VulkanContext* context{};
    VkPipelineLayout handle{};

    PipelineLayout() = default;

    static PipelineLayout CreatePipelineLayout(const VulkanContext* context, const VkPipelineLayoutCreateInfo& layoutCreateInfo);

    ~PipelineLayout();

    PipelineLayout(const PipelineLayout&) = delete;

    PipelineLayout& operator=(const PipelineLayout&) = delete;

    PipelineLayout(PipelineLayout&& other) noexcept;

    PipelineLayout& operator=(PipelineLayout&& other) noexcept;
};

struct Pipeline
{
    const VulkanContext* context{};
    VkPipeline handle{};

    Pipeline() = default;

    static Pipeline CreateGraphicsPipeline(const VulkanContext* context, const VkGraphicsPipelineCreateInfo& pipelineCreateInfo);

    static Pipeline CreateComputePipeline(const VulkanContext* context, const VkComputePipelineCreateInfo& pipelineCreateInfo);

    ~Pipeline();

    Pipeline(const Pipeline&) = delete;

    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;

    Pipeline& operator=(Pipeline&& other) noexcept;
};
} // Render

#endif //WILL_ENGINE_VK_RESOURCES_H
