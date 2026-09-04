//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_H
#define WILL_ENGINE_SCENE_H
#include "core/containers/vector.h"
#include "core/string_id.h"

namespace Engine
{
struct Scene
{
    Core::Vector<std::byte> content;
    uint32_t entityCount{0};

    Scene() = default;
    explicit Scene(Core::TlsfAllocator* alloc) : content(alloc, Core::AllocTag::AssetManager) {}
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;
};

static inline StringID GLOBAL_SCENE_ID = "global_scene"_sid;
} // Engine

#endif //WILL_ENGINE_SCENE_H
