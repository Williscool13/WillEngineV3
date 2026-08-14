//
// Created by William on 2026-03-23.
//

#ifndef WILL_ENGINE_DEBUG_GIZMO_COMPONENT_H
#define WILL_ENGINE_DEBUG_GIZMO_COMPONENT_H

#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <entt/entt.hpp>

#include "engine/component_registry.h"
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
    static constexpr const char* COMPONENT_NAME = "DebugGizmoComponent";

    DebugGizmoShape shape{DebugGizmoShape::Sphere};
    glm::vec3 extents{0.5f};
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float lineWidth{0.05f};

    static void Serialize(const DebugGizmoComponent& comp, Engine::TextWriter& w);
    static void Deserialize(DebugGizmoComponent& comp, const Engine::TextReader& r);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_DEBUG_GIZMO_COMPONENT_H
