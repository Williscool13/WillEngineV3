//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_DESCRIPTORS_H
#define WILL_ENGINE_VK_DESCRIPTORS_H

#include <cstdint>
#include <vector>

#include <volk.h>

namespace Render
{
struct DescriptorLayoutBuilder
{
    explicit DescriptorLayoutBuilder(uint32_t reservedSize = 0);

    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void AddBinding(uint32_t binding, VkDescriptorType type);

    void AddBinding(uint32_t binding, VkDescriptorType type, uint32_t count);

    void Clear();

    VkDescriptorSetLayoutCreateInfo Build(VkShaderStageFlagBits shaderStageFlags, VkDescriptorSetLayoutCreateFlags layoutCreateFlags);

    VkDescriptorSetLayout Build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};
}


#endif //WILL_ENGINE_VK_DESCRIPTORS_H