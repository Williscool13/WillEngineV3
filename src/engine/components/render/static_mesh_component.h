//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_STATIC_MESH_COMPONENT_H
#define WILL_ENGINE_STATIC_MESH_COMPONENT_H

#include <array>
#include <entt/entt.hpp>

#include "engine/core/registry.h"
#include "engine/core/model_id.h"
#include "engine/material_manager.h"
#include "engine/resources/model/model_types.h"
#include "engine/component_registry.h"
#include "engine/components/render_components.h"


namespace Engine::Component
{
struct StaticMeshComponent
{
    static constexpr const char* COMPONENT_NAME = "StaticMeshComponent";

    Engine::ModelID modelId{};
    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};
    Vec3 renderOffset{0.0f};
    Quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t nodeIndex{~0u};

    WILL_REFLECT(StaticMeshComponent,
        WILL_FIELD(modelId),
        WILL_FIELD(shadingShaderOverride),
        WILL_FIELD(lightingShaderOverride),
        WILL_FIELD(renderOffset),
        WILL_FIELD(renderRotation),
        WILL_FIELD(nodeIndex))

    static bool CanAdd(const Engine::EngineContext* ctx, const Engine::EngineState* state, const Engine::SystemRegistry& registry, entt::entity entity);
    static void OnConstruct(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnDestroy(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnEditPreview(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnEditCommit(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* name);
};

/**
 * Per-primitive deviations from a StaticMeshComponent's model defaults.
 */
struct StaticMeshOverridesComponent
{
    static constexpr const char* COMPONENT_NAME = "StaticMeshOverridesComponent";

    static constexpr size_t MaxMaterialOverrides = 32;
    static constexpr size_t MaxBlacklist = 64;

    struct MaterialOverride
    {
        uint32_t slot{0};
        Engine::MaterialID id{};

        WILL_REFLECT(MaterialOverride, WILL_FIELD(slot), WILL_FIELD(id))
    };

    Core::InlineVector<MaterialOverride, MaxMaterialOverrides> materialOverrides{};
    Core::InlineVector<uint32_t, MaxBlacklist> primitiveBlacklist{};

    WILL_REFLECT(StaticMeshOverridesComponent,
        WILL_FIELD(materialOverrides),
        WILL_FIELD(primitiveBlacklist))

    [[nodiscard]] Engine::MaterialID GetMaterialOverride(uint32_t slot) const;
    void SetMaterialOverride(uint32_t slot, Engine::MaterialID id);

    static bool CanAdd(const Engine::EngineContext* ctx, const Engine::EngineState* state, const Engine::SystemRegistry& registry, entt::entity entity);
    static void OnEditCommit(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
};

/** Load requested; StartStaticMeshLoads kicks the model load (when not frozen). */
struct StaticMeshLoadRequest
{
    static constexpr const char* COMPONENT_NAME = "StaticMeshLoadRequest";
};

/** Model load in flight; ResolveStaticMeshLoads binds primitives once it finishes. */
struct StaticMeshLoadingFlag
{
    static constexpr const char* COMPONENT_NAME = "StaticMeshLoadingFlag";
};

void UnloadStaticMesh(Engine::SystemRegistry& registry, entt::entity entity);
void LoadStaticMesh(StaticMeshComponent& component, Engine::SystemRegistry& registry, entt::entity entity);

void PruneStaticMeshOverrides(Engine::SystemRegistry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_STATIC_MESH_COMPONENT_H
