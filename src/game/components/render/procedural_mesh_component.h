//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
#define WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "../../../engine/material_manager.h"
#include "engine/resources/model/model_types.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"
#include "game/components/render_components.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct ProceduralMeshComponent
{
    static constexpr const char* COMPONENT_NAME = "ProceduralMeshComponent";

    Engine::ProceduralParams params;
    Engine::MaterialID material{};
    glm::vec4 modelFlags{1.0f, 1.0f, 0.0f, 0.0f}; // x: visible, y: include in probe bake (nonzero=included, default; 0=excluded, legacy scenes store 1), z: exclude from DDGI (0=contributes, default), w: free
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    static void Serialize(const ProceduralMeshComponent& comp, nlohmann::json& json);
    static void Deserialize(ProceduralMeshComponent& comp, const nlohmann::json& json);
    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

/** Generation requested; StartProceduralMeshLoads kicks the model build. */
struct ProceduralMeshLoadPendingTag
{};

/** Model build in flight; ResolveProceduralMeshLoads binds it once finished. */
struct ProceduralMeshLoadingTag
{};

void RecreateProceduralMesh(ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
