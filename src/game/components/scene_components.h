//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_COMPONENTS_H
#define WILL_ENGINE_SCENE_COMPONENTS_H
#include "core/string_id.h"

namespace Game::Component
{
/**
 * Basically all entities should have this
 */
struct SceneComponent
{
    StringID sceneId;
};
} // Game

#endif //WILL_ENGINE_SCENE_COMPONENTS_H