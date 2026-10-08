//
// Created by William on 2026-10-08.
//

#ifndef WILL_ENGINE_TEXTURE_ARRAY_ASSET_H
#define WILL_ENGINE_TEXTURE_ARRAY_ASSET_H

#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "engine/asset_manager_types.h"
#include "engine/compression/compression.h"
#include "engine/core/texture_array_id.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"

namespace Render
{
struct TextureArray
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
    Engine::TextureArrayID textureArrayId{};
    Engine::TextureArrayHandle selfHandle{Engine::TextureArrayHandle::INVALID};
    uint64_t dataOffset{0};
    uint64_t dataSize{0};
    uint64_t uncompressedSize{0};
    Engine::CompressionType compressionType{Engine::CompressionType::Zstd};
    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount = 0;
    uint64_t retireFrame{0};
    BindlessTextureArrayHandle bindlessHandle{};

    AllocatedImage image;
    ImageView imageView;
};
} // Render

#endif //WILL_ENGINE_TEXTURE_ARRAY_ASSET_H
