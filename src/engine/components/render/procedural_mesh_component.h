//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
#define WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "engine/core/registry.h"
#include "engine/material_manager.h"
#include "engine/resources/model/model_types.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/components/render_components.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
struct ProceduralMeshComponent
{
    static constexpr const char* COMPONENT_NAME = "ProceduralMeshComponent";

    Engine::ProceduralParams params;
    Engine::ProceduralRepeat repeat{};
    Engine::MaterialID material{};
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    WILL_REFLECT(ProceduralMeshComponent,
        WILL_FIELD(params, .key = "type"),
        WILL_FIELD(repeat),
        WILL_FIELD(material),
        WILL_FIELD(renderOffset),
        WILL_FIELD(renderRotation))

    static bool CanAdd(const Engine::Registry& registry, entt::entity entity);
    static void OnConstruct(Engine::Registry& registry, entt::entity entity);
    static void OnDestroy(Engine::Registry& registry, entt::entity entity);
    static void OnEditPreview(Engine::Registry& registry, entt::entity entity);
    static void OnEditCommit(Engine::Registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};

/** Generation requested; StartProceduralMeshLoads kicks the model build. */
struct ProceduralMeshLoadRequest
{};

/** Model build in flight; ResolveProceduralMeshLoads binds it once finished. */
struct ProceduralMeshLoadingFlag
{};

void RecreateProceduralMesh(ProceduralMeshComponent& component, Engine::Registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
