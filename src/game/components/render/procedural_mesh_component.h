//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
#define WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "engine/material_manager.h"
#include "engine/resources/model/model_types.h"
#include "engine/component_registry.h"
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
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    static void Serialize(const ProceduralMeshComponent& comp, Engine::TextWriter& w);
    static void Deserialize(ProceduralMeshComponent& comp, const Engine::TextReader& r);
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

/** Flat shape-field (de)serialization shared with ModuleMeshComponent parts; "type" (variant index) is written by the caller. */
void SerializeProceduralShape(const Engine::ProceduralParams& params, Engine::TextWriter& w);
Engine::ProceduralParams DeserializeProceduralShape(int32_t type, const Engine::TextReader& r);
}

#endif //WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
