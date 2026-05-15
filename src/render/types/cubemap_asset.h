//
// Created by William on 2026-02-09.
//

#ifndef WILL_ENGINE_CUBEMAP_ASSET_H
#define WILL_ENGINE_CUBEMAP_ASSET_H
#include "../interface/render_interface.h"
#include "core/containers/inline_path.h"
#include "engine/compression/compression.h"
#include "core/containers/inline_string.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "engine/asset_manager_types.h"
#include "engine/core/environment_map_id.h"

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
        Loading,
        Loaded,
        FailedToLoad
    };

    Core::Path source{};
    Core::InlineString<128> name{};
    Engine::EnvironmentMapID cubemapId{};
    Engine::CubemapHandle selfHandle{Engine::CubemapHandle::INVALID};
    uint64_t dataOffset{0};
    uint64_t dataSize{0};
    uint64_t uncompressedSize{0};
    Engine::CompressionType compressionType{Engine::DEFAULT_ENV_MAP_COMPRESSION};
    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount = 0;
    BindlessCubemapHandle bindlessHandle{};

    AllocatedImage image;
    ImageView imageView;

    Core::ImageAcquireOperation acquireBarrier{};
};
} // Render

#endif //WILL_ENGINE_CUBEMAP_ASSET_H