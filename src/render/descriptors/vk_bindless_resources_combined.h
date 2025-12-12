//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_BINDLESS_RESOURCES_COMBINED_H
#define WILL_ENGINE_VK_BINDLESS_RESOURCES_COMBINED_H

#include "core/allocators/handle.h"
#include "core/allocators/handle_allocator.h"
#include "render/render_constants.h"
#include "render/vk_resources.h"

namespace Render
{
struct VulkanContext;

// Phantom type for combined image samplers
struct BindlessCombinedImageSampler
{};

/// Type alias for combined image sampler handles
using BindlessCombinedHandle = Core::Handle<BindlessCombinedImageSampler>;

/**
 * Bindless descriptor buffer for combined image samplers.
 *
 * Contains one binding:
 *   - Binding 0: Array of combined image samplers (BINDLESS_COMBINED_IMAGE_SAMPLER_COUNT)
 *
 * Uses Vulkan descriptor buffers (VK_EXT_descriptor_buffer) for bindless access.
 * Handles are managed via HandleAllocator and returned on allocation for shader indexing.
 */
struct BindlessResourcesCombined
{
public:
    /// Descriptor set layout defining the bindless resource structure
    DescriptorSetLayout descriptorSetLayout{};

public:
    BindlessResourcesCombined();

    explicit BindlessResourcesCombined(VulkanContext* context);

    ~BindlessResourcesCombined();

    BindlessResourcesCombined(const BindlessResourcesCombined&) = delete;

    BindlessResourcesCombined& operator=(const BindlessResourcesCombined&) = delete;

    BindlessResourcesCombined(BindlessResourcesCombined&& other) noexcept;

    BindlessResourcesCombined& operator=(BindlessResourcesCombined&& other) noexcept;

    /**
     * Allocate a combined image sampler in the bindless array.
     * @param sampler Vulkan sampler handle
     * @param imageView Vulkan image view handle
     * @param imageLayout Image layout for sampling
     * @return Handle for the combined sampler, or Invalid if full
     */
    BindlessCombinedHandle AllocateCombined(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);

    /**
     * Force-update a combined sampler at a specific handle, bypassing allocation tracking.
     * WARNING: Only use for debugging or replacing existing allocations.
     *
     * @param handle Target handle in the combined sampler array
     * @param sampler Vulkan sampler handle
     * @param imageView Vulkan image view handle
     * @param imageLayout Image layout for sampling
     * @return true if handle was valid and updated
     */
    bool ForceAllocateCombined(BindlessCombinedHandle handle, VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);

    /**
     * Release a combined sampler binding, returning it to the free pool.
     * @param handle Handle returned from AllocateCombined
     * @return true if successfully released
     */
    bool ReleaseCombinedBinding(BindlessCombinedHandle handle);

    /**
     * Get binding info for vkCmdBindDescriptorBuffersEXT.
     * @return Descriptor buffer binding info
     */
    [[nodiscard]] VkDescriptorBufferBindingInfoEXT GetBindingInfo() const;

private:
    VulkanContext* context{};
    AllocatedBuffer buffer{}; ///< GPU buffer containing descriptor data
    VkDeviceSize descriptorSetSize{}; ///< Aligned size of the descriptor set

    Core::HandleAllocator<BindlessCombinedImageSampler, BINDLESS_COMBINED_IMAGE_SAMPLER_COUNT> combinedAllocator;
};
} // Render

#endif //WILL_ENGINE_VK_BINDLESS_RESOURCES_COMBINED_H
