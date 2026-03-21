//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_STATIC_MESH_COMPONENT_H
#define WILL_ENGINE_STATIC_MESH_COMPONENT_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "procedural_mesh_component.h"
#include "engine/core/model_id.h"
#include "engine/resources/material/material_manager.h"
#include "engine/resources/model/model_types.h"
#include "game/components/render_components.h"

namespace Game::Component
{
struct StaticMeshComponent
{
    glm::vec4 modelFlags{0.0f}; // x: visible, y: shadow-caster, zw: reserved

    std::array<PrimitiveData, 128> primitives{};
    uint8_t primitiveCount{0};

    Engine::ModelID modelId{};
    int32_t meshIndex{-1};
    std::array<Engine::MaterialID, 128> materialOverrides{};
    glm::vec3 renderOffset{0.0f};

    // Transient
    Engine::StaticModelHandle modelHandle{};

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct StaticMeshLoadingTag
{};

void RecreateStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_STATIC_MESH_COMPONENT_H
