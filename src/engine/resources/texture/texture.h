//
// Created by William on 2026-03-09.
//

#ifndef WILL_ENGINE_TEXTURE_H
#define WILL_ENGINE_TEXTURE_H

#include "../../../render/interface/render_interface.h"
#include "core/containers/inline_path.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/vulkan/vk_resources.h"
#include "engine/asset_manager_types.h"
#include "engine/core/texture_id.h"
#include "texture_format.h"

namespace Engine
{
struct Texture
{
    enum class LoadState
    {
        NotLoaded,
        Loading,
        Loaded,
        FailedToLoad
    };

    // Transient (Runtime)
    Render::BindlessTextureHandle bindlessHandle{};

    Render::AllocatedImage image{};
    Render::ImageView imageView{};
    Core::ImageAcquireOperation acquireBarrier{};

    Core::Path source{};
    Core::InlineString<128> name;
    TextureID textureId{};
    TextureHandle selfHandle{TextureHandle::INVALID};
    LoadState loadState{LoadState::NotLoaded};
    uint64_t acquireFrame{UINT64_MAX};
    enum class Origin { Disk, StaticProcedural, RuntimeProcedural };

    uint32_t refCount = 0;
    uint64_t retireFrame = 0;
    Origin origin{Origin::Disk};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t mipCount{0};
    VkFormat format{VK_FORMAT_UNDEFINED};
    uint32_t dataOffset{0};
    uint64_t dataSize{0};
    uint64_t uncompressedSize{0};
    CompressionType compressionType{DEFAULT_TEXTURE_COMPRESSION};
};
} // Engine

#endif //WILL_ENGINE_TEXTURE_H
