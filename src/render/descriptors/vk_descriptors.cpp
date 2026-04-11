//
// Created by William on 2025-12-12.
//

#include "vk_descriptors.h"

#include "render/vulkan/vk_utils.h"

namespace Render
{
DescriptorLayoutBuilder::DescriptorLayoutBuilder()
{
}

void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding newbind{};
    newbind.binding = binding;
    newbind.descriptorCount = 1;
    newbind.descriptorType = type;

    bindings.PushBack(newbind);
}

void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type, uint32_t count)
{
    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding{
        .binding = binding,
        .descriptorType = type,
        .descriptorCount = count,
    };
    bindings.PushBack(descriptorSetLayoutBinding);
}

void DescriptorLayoutBuilder::Clear()
{
    bindings.Clear();
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
        .bindingCount = static_cast<uint32_t>(bindings.Size()),
        .pBindings = bindings.Data(),
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

    info.pBindings = bindings.Data();
    info.bindingCount = static_cast<uint32_t>(bindings.Size());
    info.flags = flags;

    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

    return set;
}
}
