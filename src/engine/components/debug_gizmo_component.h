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
#include "engine/components/component_types.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
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

    WILL_REFLECT(DebugGizmoComponent,
        WILL_FIELD(shape),
        WILL_FIELD(extents, .min = 0.0f, .max = 100.0f, .speed = 0.01f),
        WILL_FIELD(color),
        WILL_FIELD(lineWidth, .min = 0.01f, .max = 1.0f, .speed = 0.005f))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};
}

#endif //WILL_ENGINE_DEBUG_GIZMO_COMPONENT_H
