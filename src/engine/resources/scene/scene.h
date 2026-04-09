//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_H
#define WILL_ENGINE_SCENE_H
#include <json/nlohmann/json.hpp>

#include "core/string_id.h"

namespace Engine
{
struct Scene
{
    nlohmann::json content;

    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;
};

static inline StringID GLOBAL_SCENE_ID = SID("global_scene");
} // Engine

#endif //WILL_ENGINE_SCENE_H