//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_STATIC_MESH_COMPONENT_H
#define WILL_ENGINE_STATIC_MESH_COMPONENT_H

#include <array>
#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/core/model_id.h"
#include "engine/material_manager.h"
#include "engine/resources/model/model_types.h"
#include "engine/engine_api.h"
#include "game/components/render_components.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct StaticMeshComponent
{
    static constexpr size_t MaxMaterialOverrides = 128;

    Vec4 modelFlags{1.0f, 1.0f, 0.0f, 0.0f}; // x: visible, y: shadow-caster, zw: reserved

    Engine::ModelID modelId{};
    int32_t meshIndex{-1};
    Core::Array<Engine::MaterialID, MaxMaterialOverrides> materialOverrides{};
    Vec3 renderOffset{0.0f};

    static void Serialize(const StaticMeshComponent& comp, nlohmann::json& json);
    static void Deserialize(StaticMeshComponent& comp, const nlohmann::json& json);
    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

struct StaticMeshLoadingTag
{};

void UnloadStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity);
void LoadStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_STATIC_MESH_COMPONENT_H
