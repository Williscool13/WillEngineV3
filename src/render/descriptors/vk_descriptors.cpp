//
// Created by William on 2025-12-12.
//

#include "vk_descriptors.h"

#include "render/vulkan/vk_utils.h"

namespace Render
{
DescriptorLayoutBuilder::DescriptorLayoutBuilder(const uint32_t reservedSize)
{
    if (reservedSize > 0) {
        bindings.reserve(reservedSize);
    }
}

void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding newbind{};
    newbind.binding = binding;
    newbind.descriptorCount = 1;
    newbind.descriptorType = type;

    bindings.push_back(newbind);
}

void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type, uint32_t count)
{
    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding{
        .binding = binding,
        .descriptorType = type,
        .descriptorCount = count,
    };
    bindings.push_back(descriptorSetLayoutBinding);
}

void DescriptorLayoutBuilder::Clear()
{
    bindings.clear();
}

VkDescriptorSetLayoutCreateInfo DescriptorLayoutBuilder::Build(const VkShaderStageFlagBits shaderStageFlags,
                                                               const VkDescriptorSetLayoutCreateFlags layoutCreateFlags)
{
    for (auto& b : bindings) {
        b.stageFlags |= shaderStageFlags;
    }

    return {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = layoutCreateFlags,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
}


VkDescriptorSetLayout DescriptorLayoutBuilder::Build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext,
                                                     VkDescriptorSetLayoutCreateFlags flags)
{
    for (auto& b : bindings) {
        b.stageFlags |= shaderStages;
    }

    VkDescriptorSetLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.pNext = pNext;

    info.pBindings = bindings.data();
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.flags = flags;

    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

    return set;
}
}
