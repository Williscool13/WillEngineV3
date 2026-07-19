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
    static constexpr size_t MaxMaterialOverrides = 32;
    static constexpr size_t MaxBlacklist = 64;

    struct MaterialOverride { uint32_t slot; Engine::MaterialID id; };

    Vec4 modelFlags{1.0f, 1.0f, 0.0f, 0.0f}; // x: visible, y: unused, z: exclude from DDGI (0=contributes, default), w: hero (excluded from the traced sun shadow)

    Engine::ModelID modelId{};
    Core::InlineVector<MaterialOverride, MaxMaterialOverrides> materialOverrides{};
    Core::InlineVector<uint32_t, MaxBlacklist> primitiveBlacklist{};
    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};
    Vec3 renderOffset{0.0f};
    Quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] Engine::MaterialID GetMaterialOverride(uint32_t slot) const;
    void SetMaterialOverride(uint32_t slot, Engine::MaterialID id);

    static void Serialize(const StaticMeshComponent& comp, nlohmann::json& json);
    static void Deserialize(StaticMeshComponent& comp, const nlohmann::json& json);
    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

/** Load requested; StartStaticMeshLoads kicks the model load (when not frozen). */
struct StaticMeshLoadPendingTag
{};

/** Model load in flight; ResolveStaticMeshLoads binds primitives once it finishes. */
struct StaticMeshLoadingTag
{};

void UnloadStaticMesh(entt::registry& registry, entt::entity entity);
void LoadStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_STATIC_MESH_COMPONENT_H
