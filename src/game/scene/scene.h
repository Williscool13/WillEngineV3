//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_H
#define WILL_ENGINE_SCENE_H
#include <json/nlohmann/json.hpp>

namespace Game
{
struct Scene
{
    nlohmann::json content;
};
} // Game

#endif //WILL_ENGINE_SCENE_H