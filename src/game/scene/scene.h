//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_H
#define WILL_ENGINE_SCENE_H
#include <json/nlohmann/json.hpp>

#include "core/string_id.h"

namespace Game
{
struct Scene
{
    nlohmann::json content;
};

struct SceneMetadata
{
    std::string name;
    StringID id;
};

static inline StringID GLOBAL_SCENE_ID = SID("global_scene");
} // Game

#endif //WILL_ENGINE_SCENE_H