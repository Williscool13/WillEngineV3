//
// Created by William on 2026-01-30.
//

#include "portal_components.h"

#include "core/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"
#include "spdlog/spdlog.h"

namespace Game::Component
{
PortalPair CreatePortalPair(Core::EngineContext* ctx, Engine::GameState* state, glm::vec3 posA, glm::quat rotA, glm::vec3 posB, glm::quat rotB)
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

    Engine::MaterialManager* materialManager = ctx->materialManager;
    Render::MeshInformation& submesh = plane->modelData.meshes[0];

    // Create Portal A (blue)
    entt::entity portalA = state->registry.create();
    {
        StaticMeshComponent renderable{};
        for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
            Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

            MaterialProperties material = primitive.materialIndex == -1
                ? Engine::CreateDefaultMaterial()
                : plane->modelData.materials[primitive.materialIndex];
            material.colorFactor = glm::vec4(0.3f, 0.6f, 1.0f, 0.5f); // Blue portal

            Engine::MaterialID matID = materialManager->CreateImmutableMaterial(material);
            materialManager->AcquireMaterial(matID);
            renderable.primitives[i] = {
                .primitiveIndex = primitive.index,
                .materialID = matID
            };
        }
        renderable.primitiveCount = submesh.primitiveProperties.size();
        renderable.modelFlags = glm::vec4(0.0f);

        TransformComponent transform{posA, rotA, glm::vec3(1.0f, 1.0f, 1.0f)};

        state->registry.emplace<TransformComponent>(portalA, transform);
        state->registry.emplace<StaticMeshComponent>(portalA, renderable);
        state->registry.emplace<RenderTransformComponent>(portalA, GetMatrix(transform), GetMatrix(transform));
        state->registry.emplace<PortalPlaneTag>(portalA);
        state->registry.emplace<PortalComponent>(portalA, entt::null, 1u);
    }

    // Create Portal B (orange)
    entt::entity portalB = state->registry.create();
    {
        StaticMeshComponent renderable{};
        for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
            Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

            MaterialProperties material = primitive.materialIndex == -1
                ? Engine::CreateDefaultMaterial()
                : plane->modelData.materials[primitive.materialIndex];
            material.colorFactor = glm::vec4(1.0f, 0.6f, 0.2f, 0.5f); // Orange portal

            Engine::MaterialID matID = materialManager->CreateImmutableMaterial(material);
            materialManager->AcquireMaterial(matID);
            renderable.primitives[i] = {
                .primitiveIndex = primitive.index,
                .materialID = matID
            };
        }
        renderable.primitiveCount = submesh.primitiveProperties.size();
        renderable.modelFlags = glm::vec4(0.0f);

        TransformComponent transform{posB, rotB, glm::vec3(1.0f)};
        state->registry.emplace<TransformComponent>(portalB, transform);
        state->registry.emplace<StaticMeshComponent>(portalB, renderable);
        state->registry.emplace<RenderTransformComponent>(portalB, GetMatrix(transform), GetMatrix(transform));
        state->registry.emplace<PortalPlaneTag>(portalB);
        state->registry.emplace<PortalComponent>(portalB, entt::null, 2u);
    }

    // Link them together
    state->registry.get<PortalComponent>(portalA).linkedPortal = portalB;
    state->registry.get<PortalComponent>(portalB).linkedPortal = portalA;

    SPDLOG_INFO("[DebugSystem] Created portal pair at ({}, {}, {}) <-> ({}, {}, {})",
                posA.x, posA.y, posA.z, posB.x, posB.y, posB.z);

    return {portalA, portalB};
}

void CreatePortalPlane(Core::EngineContext* ctx, Engine::GameState* state, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
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

    StaticMeshComponent renderable{};
    Engine::MaterialManager* materialManager = ctx->materialManager;
    Render::MeshInformation& submesh = plane->modelData.meshes[0];

    for (size_t i = 0; i < submesh.primitiveProperties.size(); ++i) {
        Render::PrimitiveProperty& primitive = submesh.primitiveProperties[i];

        MaterialProperties material = primitive.materialIndex == -1
            ? Engine::CreateDefaultMaterial()
            : plane->modelData.materials[primitive.materialIndex];
        material.colorFactor = glm::vec4(0.3f, 0.6f, 1.0f, 1.0f);

        Engine::MaterialID matID = materialManager->CreateImmutableMaterial(material);
        materialManager->AcquireMaterial(matID);

        renderable.primitives[i] = {
            .primitiveIndex = primitive.index,
            .materialID = matID
        };
    }
    renderable.primitiveCount = submesh.primitiveProperties.size();
    renderable.modelFlags = glm::vec4(0.0f);

    entt::entity planeEntity = state->registry.create();
    TransformComponent transformComp = state->registry.emplace<TransformComponent>(planeEntity, position, rotation, scale);
    state->registry.emplace<StaticMeshComponent>(planeEntity, renderable);
    state->registry.emplace<RenderTransformComponent>(planeEntity, GetMatrix(transformComp), GetMatrix(transformComp));
    state->registry.emplace<PortalPlaneTag>(planeEntity);

    SPDLOG_INFO("[DebugSystem] Created portal plane at ({}, {}, {})",
                position.x, position.y, position.z);
}
}
