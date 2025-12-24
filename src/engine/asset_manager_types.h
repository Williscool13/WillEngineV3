//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_TYPES_H
#define WILL_ENGINE_ASSET_MANAGER_TYPES_H
#include "core/allocators/handle.h"

namespace Render
{
struct WillModel;
struct Texture;
}

namespace Engine
{
using WillModelHandle = Core::Handle<Render::WillModel>;
using TextureHandle = Core::Handle<Render::Texture>;
}

#endif //WILL_ENGINE_ASSET_MANAGER_TYPES_H