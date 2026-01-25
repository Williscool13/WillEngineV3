//
// Created by William on 2026-01-25.
//

#ifndef WILL_ENGINE_PORTAL_COMPONENT_H
#define WILL_ENGINE_PORTAL_COMPONENT_H
#include <entt/entt.hpp>

#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/components/renderable_component.h"
#include "game/components/transform_component.h"
#include "game/components/render/portal_plane_component.h"
#include "glm/detail/type_quat.hpp"
#include "render/model/will_model_asset.h"
#include "spdlog/spdlog.h"

namespace Game
{
struct PortalComponent {
    entt::entity linkedPortal = entt::null;
    uint32_t stencilValue = 0;
};

struct PortalPair {
    entt::entity portalA;
    entt::entity portalB;
};

inline PortalPair CreatePortalPair(Core::EngineContext* ctx, Engine::GameState* state,
                           glm::vec3 posA, glm::quat rotA,
                           glm::vec3 posB, glm::quat rotB)
{
    if (!state->portalPlaneHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not loaded");
        return {entt::null, entt::null};
    }

    Render::WillModel* plane = ctx->assetManager->GetModel(state->portalPlaneHandle);
    if (!plane || plane->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not ready");
        return {entt::null, entt::null};
    }

    Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
    Render::MeshInformation& submesh = plane->modelData.meshes[0];

    // Create Portal A (blue)
    entt::entity portalA = state->registry.create();
    {
        RenderableComponent renderable{};
        for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
            Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

            MaterialProperties material;
            if (primitive.materialIndex != -1) {
                material = plane->modelData.materials[primitive.materialIndex];
            } else {
                material = materialManager.Get(materialManager.GetDefaultMaterial());
            }
            material.colorFactor = glm::vec4(0.3f, 0.6f, 1.0f, 0.5f); // Blue portal

            Engine::MaterialID matID = materialManager.GetOrCreate(material);
            renderable.primitives[i] = {
                .primitiveIndex = primitive.index,
                .materialID = matID
            };
        }
        renderable.primitiveCount = submesh.primitiveProperties.size();
        renderable.modelFlags = glm::vec4(0.0f);

        TransformComponent transform{posA, rotA, glm::vec3(0.02f, 0.02f, 0.01f)};
        renderable.previousModelMatrix = GetMatrix(transform);

        state->registry.emplace<TransformComponent>(portalA, transform);
        state->registry.emplace<RenderableComponent>(portalA, renderable);
        state->registry.emplace<PortalPlaneComponent>(portalA);
        state->registry.emplace<PortalComponent>(portalA, entt::null, 1u);
    }

    // Create Portal B (orange)
    entt::entity portalB = state->registry.create();
    {
        RenderableComponent renderable{};
        for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
            Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

            MaterialProperties material;
            if (primitive.materialIndex != -1) {
                material = plane->modelData.materials[primitive.materialIndex];
            } else {
                material = materialManager.Get(materialManager.GetDefaultMaterial());
            }
            material.colorFactor = glm::vec4(1.0f, 0.6f, 0.2f, 0.5f); // Orange portal

            Engine::MaterialID matID = materialManager.GetOrCreate(material);
            renderable.primitives[i] = {
                .primitiveIndex = primitive.index,
                .materialID = matID
            };
        }
        renderable.primitiveCount = submesh.primitiveProperties.size();
        renderable.modelFlags = glm::vec4(0.0f);

        TransformComponent transform{posB, rotB, glm::vec3(0.02f, 0.02f, 0.01f)};
        renderable.previousModelMatrix = GetMatrix(transform);

        state->registry.emplace<TransformComponent>(portalB, transform);
        state->registry.emplace<RenderableComponent>(portalB, renderable);
        state->registry.emplace<PortalPlaneComponent>(portalB);
        state->registry.emplace<PortalComponent>(portalB, entt::null, 2u);
    }

    // Link them together
    state->registry.get<PortalComponent>(portalA).linkedPortal = portalB;
    state->registry.get<PortalComponent>(portalB).linkedPortal = portalA;

    SPDLOG_INFO("[DebugSystem] Created portal pair at ({}, {}, {}) <-> ({}, {}, {})",
                posA.x, posA.y, posA.z, posB.x, posB.y, posB.z);

    return {portalA, portalB};
}

inline void CreatePortalPlane(Core::EngineContext* ctx, Engine::GameState* state, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
    if (!state->portalPlaneHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not loaded, press F1 first");
        return;
    }

    Render::WillModel* plane = ctx->assetManager->GetModel(state->portalPlaneHandle);
    if (!plane || plane->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not ready yet");
        return;
    }

    RenderableComponent renderable{};
    Engine::MaterialManager& materialManager = ctx->assetManager->GetMaterialManager();
    Render::MeshInformation& submesh = plane->modelData.meshes[0];

    for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
        Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

        MaterialProperties material;
        if (primitive.materialIndex != -1) {
            material = plane->modelData.materials[primitive.materialIndex];
        }
        else {
            material = materialManager.Get(materialManager.GetDefaultMaterial());
        }

        material.colorFactor = glm::vec4(0.3f, 0.6f, 1.0f, 1.0f);

        Engine::MaterialID matID = materialManager.GetOrCreate(material);

        renderable.primitives[i] = {
            .primitiveIndex = primitive.index,
            .materialID = matID
        };
    }
    renderable.primitiveCount = submesh.primitiveProperties.size();
    renderable.modelFlags = glm::vec4(0.0f);

    entt::entity planeEntity = state->registry.create();
    TransformComponent transformComp = state->registry.emplace<TransformComponent>(planeEntity, position, rotation, scale);
    renderable.previousModelMatrix = GetMatrix(transformComp);
    state->registry.emplace<RenderableComponent>(planeEntity, renderable);
    state->registry.emplace<PortalPlaneComponent>(planeEntity);

    SPDLOG_INFO("[DebugSystem] Created portal plane at ({}, {}, {})",
                position.x, position.y, position.z);
}
} // Game

#endif //WILL_ENGINE_PORTAL_COMPONENT_H