//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_TEXTURE_ASSET_H
#define WILL_ENGINE_TEXTURE_ASSET_H
#include "core/include/render_interface.h"
#include "descriptors/vk_bindless_resources_sampler_images.h"
#include "engine/asset_manager_types.h"
#include "model/will_model_asset.h"
#include "vulkan/vk_resources.h"

namespace Render
{
/**
 * Designed to be an accessible asset for standalone textures (i.e. not a model related texture)
 */
struct Texture
{
public:
    enum class LoadState
    {
        NotLoaded,
        Loaded,
        FailedToLoad
    };

    std::filesystem::path source{};
    std::string name{};
    Engine::TextureHandle selfHandle{Engine::TextureHandle::INVALID};
    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount = 0;
    BindlessTextureHandle bindlessHandle{};

    AllocatedImage image;
    ImageView imageView;

    Core::ImageAcquireOperation acquireBarrier{};
};
} // Render

#endif //WILL_ENGINE_TEXTURE_ASSET_H