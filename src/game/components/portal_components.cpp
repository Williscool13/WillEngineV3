//
// Created by William on 2026-01-30.
//

#include "portal_components.h"

#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"
#include "spdlog/spdlog.h"

namespace Game::Component
{
PortalPair CreatePortalPair(Engine::EngineContext* ctx, Engine::EngineState* state, glm::vec3 posA, glm::quat rotA, glm::vec3 posB, glm::quat rotB)
{
    if (!state->portalPlaneHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not loaded");
        return {entt::null, entt::null};
    }

    Engine::StaticModel* plane = ctx->assetManager->GetModel(state->portalPlaneHandle);
    if (!plane || plane->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not ready");
        return {entt::null, entt::null};
    }

    Engine::MaterialManager* materialManager = ctx->materialManager;
    Engine::MeshInformation& submesh = plane->modelData.meshes[0];

    auto makePortalRuntime = [&](glm::vec4 color) -> MeshRuntime {
        MeshRuntime runtime{};
        for (size_t i = 0; i < submesh.primitiveProperties.Size(); ++i) {
            Engine::PrimitiveProperty& primitive = submesh.primitiveProperties[i];
            Engine::Material material;
            if (primitive.materialIndex == -1) {
                material.props = ctx->materialManager->GetDefaultMaterialProperties();
            } else {
                material = plane->modelData.materials[primitive.materialIndex];
            }
            material.props.colorFactor = color;
            Engine::MaterialID matID = materialManager->CreateImmutableMaterial(material);
            materialManager->AcquireMaterial(matID);
            runtime.primitives.PushBack({
                .primitiveIndex = primitive.index,
                .originalMaterialIndex = primitive.materialIndex,
                .materialID = matID
            });
        }
        return runtime;
    };

    // Create Portal A (blue)
    entt::entity portalA = state->registry.create();
    {
        TransformComponent transform{posA, rotA, glm::vec3(1.0f, 1.0f, 1.0f)};
        state->registry.emplace<TransformComponent>(portalA, transform);
        state->registry.emplace<StaticMeshComponent>(portalA).modelFlags = glm::vec4(0.0f);
        state->registry.emplace<MeshRuntime>(portalA, makePortalRuntime({0.3f, 0.6f, 1.0f, 0.5f}));
        state->registry.emplace<RenderTransformComponent>(portalA, GetMatrix(transform), GetMatrix(transform));
        state->registry.emplace<PortalPlaneTag>(portalA);
        state->registry.emplace<PortalComponent>(portalA, entt::null, 1u);
    }

    // Create Portal B (orange)
    entt::entity portalB = state->registry.create();
    {
        TransformComponent transform{posB, rotB, glm::vec3(1.0f)};
        state->registry.emplace<TransformComponent>(portalB, transform);
        state->registry.emplace<StaticMeshComponent>(portalB).modelFlags = glm::vec4(0.0f);
        state->registry.emplace<MeshRuntime>(portalB, makePortalRuntime({1.0f, 0.6f, 0.2f, 0.5f}));
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

void CreatePortalPlane(Engine::EngineContext* ctx, Engine::EngineState* state, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
    if (!state->portalPlaneHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not loaded, press F1 first");
        return;
    }

    Engine::StaticModel* plane = ctx->assetManager->GetModel(state->portalPlaneHandle);
    if (!plane || plane->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Portal plane model not ready yet");
        return;
    }

    Engine::MaterialManager* materialManager = ctx->materialManager;
    Engine::MeshInformation& submesh = plane->modelData.meshes[0];

    MeshRuntime runtime{};
    for (size_t i = 0; i < submesh.primitiveProperties.Size(); ++i) {
        Engine::PrimitiveProperty& primitive = submesh.primitiveProperties[i];
        Engine::Material material;
        if (primitive.materialIndex == -1) {
            material.props = ctx->materialManager->GetDefaultMaterialProperties();
        } else {
            material = plane->modelData.materials[primitive.materialIndex];
        }
        material.props.colorFactor = glm::vec4(0.3f, 0.6f, 1.0f, 1.0f);
        Engine::MaterialID matID = materialManager->CreateImmutableMaterial(material);
        materialManager->AcquireMaterial(matID);
        runtime.primitives.PushBack({
            .primitiveIndex = primitive.index,
            .originalMaterialIndex = primitive.materialIndex,
            .materialID = matID
        });
    }

    entt::entity planeEntity = state->registry.create();
    TransformComponent transformComp = state->registry.emplace<TransformComponent>(planeEntity, position, rotation, scale);
    state->registry.emplace<StaticMeshComponent>(planeEntity).modelFlags = glm::vec4(0.0f);
    state->registry.emplace<MeshRuntime>(planeEntity, std::move(runtime));
    state->registry.emplace<RenderTransformComponent>(planeEntity, GetMatrix(transformComp), GetMatrix(transformComp));
    state->registry.emplace<PortalPlaneTag>(planeEntity);

    SPDLOG_INFO("[DebugSystem] Created portal plane at ({}, {}, {})",
                position.x, position.y, position.z);
}
}
