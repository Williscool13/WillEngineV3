//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_DESCRIPTORS_H
#define WILL_ENGINE_VK_DESCRIPTORS_H

#include <volk.h>

#include "core/containers/inline_vector.h"

namespace Render
{
struct DescriptorLayoutBuilder
{
    explicit DescriptorLayoutBuilder();

    Core::InlineVector<VkDescriptorSetLayoutBinding, 16> bindings;

    void AddBinding(uint32_t binding, VkDescriptorType type);

    void AddBinding(uint32_t binding, VkDescriptorType type, uint32_t count);

    void Clear();

    VkDescriptorSetLayoutCreateInfo Build(VkShaderStageFlagBits shaderStageFlags, VkDescriptorSetLayoutCreateFlags layoutCreateFlags);

    VkDescriptorSetLayout Build(VkDevice device, const VkAllocationCallbacks* hostAllocCallbacks, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};
}


#endif //WILL_ENGINE_VK_DESCRIPTORS_H