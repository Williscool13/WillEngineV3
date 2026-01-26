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
struct WillModel;
struct Texture;
}

namespace Engine
{
using WillModelHandle = Core::Handle<Render::WillModel>;
using TextureHandle = Core::Handle<Render::Texture>;
using AudioHandle = Core::Handle<Audio::WillAudio>;
}

#endif //WILL_ENGINE_ASSET_MANAGER_TYPES_H