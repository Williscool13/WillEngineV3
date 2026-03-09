//
// Created by William on 2026-03-09.
//

#ifndef WILL_ENGINE_TEXTURE_H
#define WILL_ENGINE_TEXTURE_H

#include <filesystem>
#include <string>

#include "core/include/render_interface.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/vulkan/vk_resources.h"
#include "engine/asset_manager_types.h"
#include "engine/core/texture_id.h"

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

    std::filesystem::path source{};
    std::string name{};
    TextureID textureId{};
    TextureHandle selfHandle{TextureHandle::INVALID};
    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount = 0;

    // Transient (Runtime)
    Render::BindlessTextureHandle bindlessHandle{};

    Render::AllocatedImage image;
    Render::ImageView imageView;
    Core::ImageAcquireOperation acquireBarrier{};
};
} // Engine

#endif //WILL_ENGINE_TEXTURE_H
