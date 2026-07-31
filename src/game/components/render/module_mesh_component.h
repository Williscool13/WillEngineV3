//
// Created by William on 2026-07-31.
//

#ifndef WILL_ENGINE_MODULE_MESH_COMPONENT_H
#define WILL_ENGINE_MODULE_MESH_COMPONENT_H

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "engine/core/material_id.h"
#include "engine/resources/model/model_types.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"
#include "core/containers/array.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
/**
 * A script-authored kit piece: ModuleParams parts baked into one content-hashed model with one
 * primitive per material slot (parts sharing a slot merge). slotMaterials re-skins per instance at
 * resolve time; an invalid slot material falls back to the default material. Modules deliberately
 * have no collider asset: attach a few per-part procedural collider shapes on the PhysicsBodyDesc.
 */
struct ModuleMeshComponent
{
    static constexpr const char* COMPONENT_NAME = "ModuleMeshComponent";

    Engine::ModuleParams params{};
    Core::Array<Engine::MaterialID, Engine::MAX_MODULE_SLOTS> slotMaterials{};
    glm::vec4 modelFlags{1.0f, 1.0f, 0.0f, 0.0f}; // x: visible, y: include in probe bake (nonzero=included, default), z: exclude from DDGI (0=contributes, default), w: free
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    static void Serialize(const ModuleMeshComponent& comp, nlohmann::json& json);
    static void Deserialize(ModuleMeshComponent& comp, const nlohmann::json& json);
    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

/** Generation requested; ModuleMeshPendingKickoff kicks the model build. */
struct ModuleMeshLoadPendingTag
{};

/** Model build in flight; ModuleMeshLoadResolve binds it once finished. */
struct ModuleMeshLoadingTag
{};
}

#endif //WILL_ENGINE_MODULE_MESH_COMPONENT_H
