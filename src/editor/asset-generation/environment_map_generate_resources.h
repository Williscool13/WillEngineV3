//
// Created by William on 2026-02-11.
//

#ifndef WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_RESOURCES_H
#define WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_RESOURCES_H

#include <vulkan/vulkan_core.h>

#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;
}

namespace Editor
{
class EnvironmentMapGenerateResources
{
public:
    Render::DescriptorSetLayout descriptorSetLayout;

    EnvironmentMapGenerateResources();
    explicit EnvironmentMapGenerateResources(Render::VulkanContext* context);
    ~EnvironmentMapGenerateResources();

    EnvironmentMapGenerateResources(const EnvironmentMapGenerateResources&) = delete;
    EnvironmentMapGenerateResources& operator=(const EnvironmentMapGenerateResources&) = delete;

    EnvironmentMapGenerateResources(EnvironmentMapGenerateResources&& other) noexcept;
    EnvironmentMapGenerateResources& operator=(EnvironmentMapGenerateResources&& other) noexcept;

    bool SetSampler(VkSampler sampler, uint32_t index) const;

    bool SetTexture2D(const VkDescriptorImageInfo& imageInfo, uint32_t index) const;

    bool SetCubemap(const VkDescriptorImageInfo& imageInfo, uint32_t index) const;

    bool SetRWCubemapArray(const VkDescriptorImageInfo& imageInfo, uint32_t index) const;

    [[nodiscard]] VkDescriptorBufferBindingInfoEXT GetBindingInfo() const;

private:
    Render::VulkanContext* context = nullptr;
    Render::AllocatedBuffer buffer;
    VkDeviceSize descriptorSetSize = 0;

    static constexpr uint32_t MAX_SAMPLERS = 8;
    static constexpr uint32_t MAX_TEXTURES_2D = 8;
    static constexpr uint32_t MAX_CUBEMAPS = 8;
    static constexpr uint32_t MAX_RW_CUBEMAP_ARRAYS = 8;
};

} // Editor

#endif //WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_RESOURCES_H