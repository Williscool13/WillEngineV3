//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_TYPES_H
#define WILL_ENGINE_ASSET_MANAGER_TYPES_H
#include "core/memory/handle.h"

namespace Audio
{
struct WillAudio;
}

namespace Render
{
struct Cubemap;
struct TextureArray;
}

namespace Engine
{
struct StaticModel;
struct Sampler;
struct Texture;
struct Font;
struct PhysicsColliderAsset;

using StaticModelHandle = Core::Handle<StaticModel>;
using TextureHandle = Core::Handle<Texture>;
using SamplerHandle = Core::Handle<Sampler>;
using CubemapHandle = Core::Handle<Render::Cubemap>;
using TextureArrayHandle = Core::Handle<Render::TextureArray>;
using AudioHandle = Core::Handle<Audio::WillAudio>;
using FontHandle = Core::Handle<Font>;
using PhysicsColliderHandle = Core::Handle<PhysicsColliderAsset>;
}

#endif //WILL_ENGINE_ASSET_MANAGER_TYPES_H