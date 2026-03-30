//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_SPLINE_MESH_COMPONENT_H
#define WILL_ENGINE_SPLINE_MESH_COMPONENT_H

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "procedural_mesh_component.h"
#include "engine/asset_manager_types.h"
#include "engine/core/material_id.h"
#include "engine/spline/spline.h"
#include "core/memory/inline_vector.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct SplineMeshComponent
{
    Engine::Spline spline;
    float radius{0.5f};
    float rollAngle{0.0f};
    int32_t sides{8};
    int32_t segmentsPerSpan{8};
    bool bCaps{true};
    bool bDualPath{false};
    float dualPathSpacing{1.0f};
    bool bCrossPlanks{false};
    int32_t crossPlankInterval{4};
    float crossPlankHeight{0.0f};

    Engine::MaterialID material{};
    glm::vec4 modelFlags{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec3 renderOffset{0.0f};

    static void Serialize(const SplineMeshComponent& comp, nlohmann::json& json);
    static void Deserialize(SplineMeshComponent& comp, const nlohmann::json& json);
    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

struct SplineMeshLoadingTag
{};
}

#endif //WILL_ENGINE_SPLINE_MESH_COMPONENT_H
