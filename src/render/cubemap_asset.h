//
// Created by William on 2026-02-09.
//

#ifndef WILL_ENGINE_CUBEMAP_ASSET_H
#define WILL_ENGINE_CUBEMAP_ASSET_H
#include <filesystem>

#include "core/include/render_interface.h"
#include "descriptors/vk_bindless_resources_sampler_images.h"
#include "engine/asset_manager_types.h"

namespace Render
{
/**
 * Designed to be an accessible asset for standalone cubemaps
 */
struct Cubemap
{
    enum class LoadState
    {
        NotLoaded,
        Loaded,
        FailedToLoad
    };

    std::filesystem::path source{};
    std::string name{};
    Engine::CubemapHandle selfHandle{Engine::CubemapHandle::INVALID};
    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount = 0;
    BindlessCubemapHandle bindlessHandle{};

    AllocatedImage image;
    ImageView imageView;

    Core::ImageAcquireOperation acquireBarrier{};
};
} // Render

#endif //WILL_ENGINE_CUBEMAP_ASSET_H