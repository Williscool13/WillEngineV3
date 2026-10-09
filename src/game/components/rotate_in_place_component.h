//
// Created by William on 2026-06-16.
//

#ifndef WILL_ENGINE_ROTATE_IN_PLACE_COMPONENT_H
#define WILL_ENGINE_ROTATE_IN_PLACE_COMPONENT_H

#include <glm/vec3.hpp>
#include <entt/entt.hpp>

#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct RotateInPlaceComponent
{
    static constexpr const char* COMPONENT_NAME = "RotateInPlaceComponent";
    static constexpr bool MOVES_ENTITY = true;

    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float speedDegrees{45.0f};
    bool bWorldSpace{false};

    WILL_REFLECT(RotateInPlaceComponent,
        WILL_FIELD(axis),
        WILL_FIELD(speedDegrees, .speed = 1.0f),
        WILL_FIELD(bWorldSpace))


    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};
}

#endif //WILL_ENGINE_ROTATE_IN_PLACE_COMPONENT_H
