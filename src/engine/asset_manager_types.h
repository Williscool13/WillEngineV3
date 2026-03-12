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
}

namespace Engine
{
struct StaticModel;
struct Sampler;
struct Texture;

using StaticModelHandle = Core::Handle<StaticModel>;
using TextureHandle = Core::Handle<Texture>;
using SamplerHandle = Core::Handle<Sampler>;
using CubemapHandle = Core::Handle<Render::Cubemap>;
using AudioHandle = Core::Handle<Audio::WillAudio>;
}

#endif //WILL_ENGINE_ASSET_MANAGER_TYPES_H