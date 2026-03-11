//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_TYPES_H
#define WILL_ENGINE_ASSET_MANAGER_TYPES_H
#include "core/allocators/handle.h"

namespace Audio
{
struct WillAudio;
}

namespace Render
{
struct Cubemap;
struct WillModel;
}

namespace Engine
{
struct Sampler;
struct Texture;

using WillModelHandle = Core::Handle<Render::WillModel>;
using TextureHandle = Core::Handle<Texture>;
using SamplerHandle = Core::Handle<Sampler>;
using CubemapHandle = Core::Handle<Render::Cubemap>;
using AudioHandle = Core::Handle<Audio::WillAudio>;
}

#endif //WILL_ENGINE_ASSET_MANAGER_TYPES_H