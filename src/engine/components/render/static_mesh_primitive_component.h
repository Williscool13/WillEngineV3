//
// Created by William on 2026-07-02.
//

#ifndef WILL_ENGINE_STATIC_MESH_PRIMITIVE_COMPONENT_H
#define WILL_ENGINE_STATIC_MESH_PRIMITIVE_COMPONENT_H

#include <entt/entt.hpp>

#include "engine/core/registry.h"
#include "engine/core/model_id.h"
#include "engine/material_manager.h"
#include "engine/component_registry.h"
#include "engine/components/render_components.h"


namespace Engine::Component
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

    WILL_REFLECT(StaticMeshPrimitiveComponent,
        WILL_FIELD(modelId),
        WILL_FIELD(primitiveOrdinal),
        WILL_FIELD(materialOverride),
        WILL_FIELD(shadingShaderOverride),
        WILL_FIELD(lightingShaderOverride),
        WILL_FIELD(renderOffset, .speed = 0.1f),
        WILL_FIELD(renderRotation))

    static bool CanAdd(const Engine::EngineContext* ctx, const Engine::EngineState* state, const Engine::SystemRegistry& registry, entt::entity entity);
    static void OnConstruct(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnDestroy(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnEditPreview(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnEditCommit(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* name);
};

struct StaticMeshPrimitiveLoadRequest
{
    static constexpr const char* COMPONENT_NAME = "StaticMeshPrimitiveLoadRequest";
};

struct StaticMeshPrimitiveLoadingFlag
{
    static constexpr const char* COMPONENT_NAME = "StaticMeshPrimitiveLoadingFlag";
};

void UnloadStaticMeshPrimitive(Engine::SystemRegistry& registry, entt::entity entity);
void LoadStaticMeshPrimitive(StaticMeshPrimitiveComponent& component, Engine::SystemRegistry& registry, entt::entity entity);
}

#endif //WILL_ENGINE_STATIC_MESH_PRIMITIVE_COMPONENT_H
