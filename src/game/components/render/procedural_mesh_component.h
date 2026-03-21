//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
#define WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "engine/resources/material/material_manager.h"
#include "engine/resources/model/model_types.h"
#include "game/components/render_components.h"

namespace Game::Component
{
struct PrimitiveData
{
    uint32_t primitiveIndex;
    int32_t originalMaterialIndex;
    Engine::MaterialID materialID;
};

struct ProceduralMeshComponent
{
    Engine::ProceduralParams params;
    Engine::MaterialID material{};
    glm::vec4 modelFlags{0.0f};
    glm::vec3 renderOffset{0.0f};

    // Transient
    Engine::StaticModelHandle modelHandle{};
    PrimitiveData primitive{};
    bool bPrimitiveReady{false};

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct ProceduralMeshLoadingTag
{};

void RecreateProceduralMesh(ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_PROCEDURAL_MESH_COMPONENT_H
