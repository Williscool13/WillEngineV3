//
// Created by William on 2026-03-11.
//

#ifndef WILL_ENGINE_SAMPLER_H
#define WILL_ENGINE_SAMPLER_H
#include "engine/asset_manager_types.h"
#include "engine/core/sampler_id.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/vulkan/vk_resources.h"

namespace Engine
{
struct SamplerDesc
{
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float mipLodBias = 0.0f;
    float minLod = 0.0f;
    float maxLod = VK_LOD_CLAMP_NONE;
    VkBool32 anisotropyEnable = VK_FALSE;
    float maxAnisotropy = 1.0f;

    [[nodiscard]] VkSamplerCreateInfo ToVkSamplerCreateInfo() const
    {
        return VkSamplerCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = magFilter,
            .minFilter = minFilter,
            .mipmapMode = mipmapMode,
            .addressModeU = addressModeU,
            .addressModeV = addressModeV,
            .addressModeW = addressModeW,
            .mipLodBias = mipLodBias,
            .anisotropyEnable = anisotropyEnable,
            .maxAnisotropy = maxAnisotropy,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = minLod,
            .maxLod = maxLod,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
    }

    bool operator==(const SamplerDesc&) const = default;
};

struct Sampler
{
    SamplerDesc desc;
    SamplerID id{SamplerID::INVALID};

    SamplerHandle selfHandle{SamplerHandle::INVALID};
    uint32_t refCount = 0;

    Render::Sampler sampler;
    Render::BindlessSamplerHandle bindlessHandle{Render::BindlessSamplerHandle::INVALID};
};
} // Engine

#endif //WILL_ENGINE_SAMPLER_H
