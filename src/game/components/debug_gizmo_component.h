//
// Created by William on 2026-03-23.
//

#ifndef WILL_ENGINE_DEBUG_GIZMO_COMPONENT_H
#define WILL_ENGINE_DEBUG_GIZMO_COMPONENT_H

#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
enum class DebugGizmoShape : uint8_t
{
    None,
    Sphere,
    Box,
    Count
};

struct DebugGizmoComponent
{
    DebugGizmoShape shape{DebugGizmoShape::Sphere};
    glm::vec3 extents{0.5f};
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float lineWidth{0.05f};

    static void Serialize(const DebugGizmoComponent& comp, nlohmann::json& json);
    static void Deserialize(DebugGizmoComponent& comp, const nlohmann::json& json);
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_DEBUG_GIZMO_COMPONENT_H
