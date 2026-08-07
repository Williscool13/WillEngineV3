//
// Created by William on 2026-07-02.
//

#ifndef WILL_ENGINE_STATIC_MESH_PRIMITIVE_COMPONENT_H
#define WILL_ENGINE_STATIC_MESH_PRIMITIVE_COMPONENT_H

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/core/model_id.h"
#include "engine/material_manager.h"
#include "engine/component_registry.h"
#include "game/components/render_components.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
/**
 * One primitive of a model, split off from a whole-model StaticMeshComponent
 */
struct StaticMeshPrimitiveComponent
{
    static constexpr const char* COMPONENT_NAME = "StaticMeshPrimitiveComponent";

    Engine::ModelID modelId{};
    uint32_t primitiveOrdinal{~0u};
    Engine::MaterialID materialOverride{};
    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};
    Vec3 renderOffset{0.0f};
    Quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    static void Serialize(const StaticMeshPrimitiveComponent& comp, nlohmann::json& json);
    static void Deserialize(StaticMeshPrimitiveComponent& comp, const nlohmann::json& json);
    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

struct StaticMeshPrimitiveLoadPendingTag
{};

struct StaticMeshPrimitiveLoadingTag
{};

void UnloadStaticMeshPrimitive(entt::registry& registry, entt::entity entity);
void LoadStaticMeshPrimitive(StaticMeshPrimitiveComponent& component, entt::registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_STATIC_MESH_PRIMITIVE_COMPONENT_H
