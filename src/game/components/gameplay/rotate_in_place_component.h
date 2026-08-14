//
// Created by William on 2026-06-16.
//

#ifndef WILL_ENGINE_ROTATE_IN_PLACE_COMPONENT_H
#define WILL_ENGINE_ROTATE_IN_PLACE_COMPONENT_H

#include <glm/vec3.hpp>
#include <entt/entt.hpp>

#include "engine/component_registry.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct RotateInPlaceComponent
{
    static constexpr const char* COMPONENT_NAME = "RotateInPlaceComponent";

    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float speedDegrees{45.0f};
    bool bWorldSpace{false};

    static void Serialize(const RotateInPlaceComponent& comp, Engine::TextWriter& w);

    static void Deserialize(RotateInPlaceComponent& comp, const Engine::TextReader& r);

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_ROTATE_IN_PLACE_COMPONENT_H
