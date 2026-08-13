//
// Created by William on 2026-05-21.
//

#ifndef WILL_ENGINE_PROCEDURAL_TEXTURE_GENERATE_RESOURCES_H
#define WILL_ENGINE_PROCEDURAL_TEXTURE_GENERATE_RESOURCES_H

#include <vulkan/vulkan_core.h>

#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

/**
 * Descriptor buffer for procedural texture compute shader generation.
 *
 * Binding layout (set 0):
 *   - Binding 0: Sampler array (MAX_SAMPLERS)
 *   - Binding 1: RWTexture2D<float4> storage image array (MAX_RW_TEXTURES)
 *
 * Binding 1 index convention: slot * MAX_MIPS_PER_SLOT + mip (one storage view per mip, for compute mip downsample).
 * Push constant convention: the first uint32_t field must be outputIndex (binding 1 slot).
 * ProceduralTextureLoadSlot fills outputIndex = slotHandle.index * MAX_MIPS_PER_SLOT before dispatch.
 */
class ProceduralTextureGenerateResources
{
public:
    DescriptorSetLayout descriptorSetLayout{};

    ProceduralTextureGenerateResources();
    explicit ProceduralTextureGenerateResources(VulkanContext* context);
    ~ProceduralTextureGenerateResources();

    ProceduralTextureGenerateResources(const ProceduralTextureGenerateResources&) = delete;
    ProceduralTextureGenerateResources& operator=(const ProceduralTextureGenerateResources&) = delete;

    ProceduralTextureGenerateResources(ProceduralTextureGenerateResources&& other) noexcept;
    ProceduralTextureGenerateResources& operator=(ProceduralTextureGenerateResources&& other) noexcept;

    bool SetSampler(VkSampler sampler, uint32_t index) const;

    bool SetRWTexture(const VkDescriptorImageInfo& imageInfo, uint32_t index) const;

    [[nodiscard]] VkDescriptorBufferBindingInfoEXT GetBindingInfo() const;

    static constexpr uint32_t MAX_SAMPLERS = 4;
    static constexpr uint32_t MAX_TEXTURE_SLOTS = 8;
    static constexpr uint32_t MAX_MIPS_PER_SLOT = 14;
    static constexpr uint32_t MAX_RW_TEXTURES = MAX_TEXTURE_SLOTS * MAX_MIPS_PER_SLOT;

private:
    VulkanContext* context{nullptr};
    AllocatedBuffer buffer{};
    VkDeviceSize descriptorSetSize{0};
};
} // Render

#endif //WILL_ENGINE_PROCEDURAL_TEXTURE_GENERATE_RESOURCES_H
