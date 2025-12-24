//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_BINDLESS_RESOURCES_SAMPLER_IMAGES_H
#define WILL_ENGINE_BINDLESS_RESOURCES_SAMPLER_IMAGES_H

#include "core/allocators/handle.h"
#include "core/allocators/handle_allocator.h"
#include "render/render_config.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

// Phantom types for type-safe handles
struct BindlessSampler
{};

struct BindlessTexture
{};

using BindlessSamplerHandle = Core::Handle<BindlessSampler>;
using BindlessTextureHandle = Core::Handle<BindlessTexture>;

/**
 * Bindless descriptor buffer for samplers and sampled images.
 *
 * Contains two bindings:
 *   - Binding 0: Array of samplers (BINDLESS_SAMPLER_COUNT)
 *   - Binding 1: Array of sampled images (BINDLESS_SAMPLED_IMAGE_COUNT)
 *
 * Uses Vulkan descriptor buffers (VK_EXT_descriptor_buffer) for bindless access.
 * Handles are managed via HandleAllocator and returned on allocation for shader indexing.
 */
struct BindlessResourcesSamplerImages
{
public:
    /// Descriptor set layout defining the bindless resource structure
    DescriptorSetLayout descriptorSetLayout{};

public:
    BindlessResourcesSamplerImages();

    explicit BindlessResourcesSamplerImages(VulkanContext* context);

    ~BindlessResourcesSamplerImages();

    BindlessResourcesSamplerImages(const BindlessResourcesSamplerImages&) = delete;

    BindlessResourcesSamplerImages& operator=(const BindlessResourcesSamplerImages&) = delete;

    BindlessResourcesSamplerImages(BindlessResourcesSamplerImages&& other) noexcept;

    BindlessResourcesSamplerImages& operator=(BindlessResourcesSamplerImages&& other) noexcept;

    /**
     * Allocate a sampler in the bindless array.
     * @param sampler Vulkan sampler handle to bind
     * @return Handle for the sampler, or Invalid if full
     */
    BindlessSamplerHandle AllocateSampler(VkSampler sampler);

    /**
     * Allocate a texture in the bindless array.
     * @param imageInfo Descriptor info for the sampled image
     * @return Handle for the texture, or Invalid if full
     */
    BindlessTextureHandle AllocateTexture(const VkDescriptorImageInfo& imageInfo);

    BindlessTextureHandle ReserveAllocateTexture();

    /**
     * Release a sampler binding, returning it to the free pool.
     * @param handle Handle returned from AllocateSampler
     * @return true if successfully released
     */
    bool ReleaseSamplerBinding(BindlessSamplerHandle handle);

    /**
     * Release a texture binding, returning it to the free pool.
     * @param handle Handle returned from AllocateTexture
     * @return true if successfully released
     */
    bool ReleaseTextureBinding(BindlessTextureHandle handle);

    /**
     * Force-update a texture at a specific index, bypassing allocation tracking.
     * WARNING: Only use for debugging or replacing existing allocations.
     *
     * @param handle Target handle in the texture array
     * @param imageInfo New descriptor info
     * @return true if handle was valid and updated
     */
    bool ForceAllocateTexture(BindlessTextureHandle handle, const VkDescriptorImageInfo& imageInfo);

    /**
     * Get binding info for vkCmdBindDescriptorBuffersEXT.
     * @return Descriptor buffer binding info
     */
    [[nodiscard]] VkDescriptorBufferBindingInfoEXT GetBindingInfo() const;

private:
    VulkanContext* context{};
    AllocatedBuffer buffer{};
    VkDeviceSize descriptorSetSize{};

    Core::HandleAllocator<BindlessSampler, BINDLESS_SAMPLER_COUNT> samplerAllocator;
    Core::HandleAllocator<BindlessTexture, BINDLESS_SAMPLED_IMAGE_COUNT> textureAllocator;
};
} // Render

#endif //WILL_ENGINE_BINDLESS_RESOURCES_SAMPLER_IMAGES_H
