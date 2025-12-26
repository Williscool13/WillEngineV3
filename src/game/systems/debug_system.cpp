//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include <Jolt/Jolt.h>
#include <spdlog/spdlog.h>

#include "asset-load/asset_load_config.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"
#include "physics/physics_system.h"
#include "platform/paths.h"


namespace Game::System
{
static Engine::WillModelHandle dragonHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle boxHandle = Engine::WillModelHandle::INVALID;
static Engine::TextureHandle textureHandle = Engine::TextureHandle::INVALID;
static JPH::BodyID boxBodyID;
static JPH::BodyID floorBodyID;

void CreateBox(Core::EngineContext* ctx, Engine::GameState* state, glm::vec3 position)
{
    if (!boxHandle.IsValid()) {
        SPDLOG_WARN("[DebugSystem] No box model loaded, press F1 first");
        return;
    }

    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    JPH::BoxShapeSettings boxShapeSettings(JPH::Vec3(0.5f, 0.5f, 0.5f));
    boxShapeSettings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult boxShapeResult = boxShapeSettings.Create();
    JPH::ShapeRefC boxShape = boxShapeResult.Get();

    JPH::BodyCreationSettings boxSettings(
        boxShape,
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Physics::Layers::MOVING
    );

    boxBodyID = bodyInterface.CreateAndAddBody(boxSettings, JPH::EActivation::Activate);

    Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
    if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[DebugSystem] Model not ready yet");
        return;
    }

    auto& modelAllocator = ctx->assetManager->GetModelAllocator();
    auto& instanceAllocator = ctx->assetManager->GetInstanceAllocator();
    auto& materialAllocator = ctx->assetManager->GetMaterialAllocator();

    Engine::ModelHandle modelEntry = modelAllocator.Add();
    Engine::InstanceHandle instanceEntry = instanceAllocator.Add();
    Engine::MaterialHandle materialEntry = materialAllocator.Add();

    if (!modelEntry.IsValid() || !instanceEntry.IsValid() || !materialEntry.IsValid()) {
        SPDLOG_ERROR("[DebugSystem] Failed to allocate GPU slots");
        modelAllocator.Remove(modelEntry);
        instanceAllocator.Remove(instanceEntry);
        materialAllocator.Remove(materialEntry);
        return;
    }

    RenderableComponent renderable{};
    renderable.modelEntry = modelEntry;
    renderable.instanceEntry = instanceEntry;
    renderable.materialEntry = materialEntry;
    renderable.modelFlags = glm::vec4(0.0f);

    renderable.instance.primitiveIndex = model->modelData.meshes[0].primitiveIndices[0].index;
    renderable.instance.modelIndex = modelEntry.index;
    renderable.instance.materialIndex = materialEntry.index;
    renderable.instance.jointMatrixOffset = 0;
    renderable.instance.bIsAllocated = 1;

    if (model->modelData.materials.empty()) {
        renderable.material = MaterialProperties{};
        renderable.material.colorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        renderable.material.metalRoughFactors = {0.0f, 1.0f, 0.0f, 0.0f};
    }
    else {
        renderable.material = model->modelData.materials[0];
        renderable.material.textureImageIndices.x = 3;
    }

    entt::entity boxEntity = state->registry.create();
    state->registry.emplace<RenderableComponent>(boxEntity, renderable);
    TransformComponent transformComponent = state->registry.emplace<TransformComponent>(boxEntity, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    state->registry.emplace<PhysicsBodyComponent>(boxEntity, boxBodyID, transformComponent.translation, transformComponent.rotation);
}

void CreateWall(Core::EngineContext* ctx, JPH::RVec3 position, JPH::Vec3 halfExtents)
{
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    JPH::BoxShapeSettings wallShapeSettings(halfExtents);
    wallShapeSettings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult wallShapeResult = wallShapeSettings.Create();
    JPH::ShapeRefC wallShape = wallShapeResult.Get();

    JPH::BodyCreationSettings wallSettings(
        wallShape,
        position,
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Physics::Layers::NON_MOVING
    );

    bodyInterface.CreateAndAddBody(wallSettings, JPH::EActivation::DontActivate);
}

void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (state->inputFrame->GetKey(Key::F1).pressed) {
        if (!dragonHandle.IsValid()) {
            dragonHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "dragon/dragon.willmodel");
            boxHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "BoxTextured.willmodel");
        }
    }

    if (state->inputFrame->GetKey(Key::F2).pressed) {
        if (!boxHandle.IsValid()) {
            SPDLOG_WARN("[DebugSystem] No box model loaded, press F1 first");
            return;
        }

        auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();
        JPH::BoxShapeSettings floorShapeSettings(JPH::Vec3(10.0f, 0.5f, 10.0f));
        floorShapeSettings.SetEmbedded();
        JPH::ShapeSettings::ShapeResult floorShapeResult = floorShapeSettings.Create();
        JPH::ShapeRefC floorShape = floorShapeResult.Get();
        JPH::BodyCreationSettings floorSettings(
            floorShape,
            JPH::RVec3(0.0, -0.5, 0.0),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            Physics::Layers::NON_MOVING
        );
        floorBodyID = bodyInterface.CreateAndAddBody(floorSettings, JPH::EActivation::DontActivate);

        // Create renderable for floor
        Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
        if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Model not ready yet");
            return;
        }

        auto& modelAllocator = ctx->assetManager->GetModelAllocator();
        auto& instanceAllocator = ctx->assetManager->GetInstanceAllocator();
        auto& materialAllocator = ctx->assetManager->GetMaterialAllocator();

        Engine::ModelHandle modelEntry = modelAllocator.Add();
        Engine::InstanceHandle instanceEntry = instanceAllocator.Add();
        Engine::MaterialHandle materialEntry = materialAllocator.Add();

        if (!modelEntry.IsValid() || !instanceEntry.IsValid() || !materialEntry.IsValid()) {
            SPDLOG_ERROR("[DebugSystem] Failed to allocate GPU slots");
            modelAllocator.Remove(modelEntry);
            instanceAllocator.Remove(instanceEntry);
            materialAllocator.Remove(materialEntry);
            return;
        }

        RenderableComponent renderable{};
        renderable.modelEntry = modelEntry;
        renderable.instanceEntry = instanceEntry;
        renderable.materialEntry = materialEntry;
        renderable.modelFlags = glm::vec4(0.0f);

        renderable.instance.primitiveIndex = model->modelData.meshes[0].primitiveIndices[0].index;
        renderable.instance.modelIndex = modelEntry.index;
        renderable.instance.materialIndex = materialEntry.index;
        renderable.instance.jointMatrixOffset = 0;
        renderable.instance.bIsAllocated = 1;

        if (model->modelData.materials.empty()) {
            renderable.material = MaterialProperties{};
            renderable.material.colorFactor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f); // gray floor
            renderable.material.metalRoughFactors = {0.0f, 1.0f, 0.0f, 0.0f};
        }
        else {
            renderable.material = model->modelData.materials[0];
            renderable.material.textureImageIndices.x = AssetLoad::ERROR_IMAGE_BINDLESS_INDEX;
            renderable.material.textureSamplerIndices.x = AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX;
        }

        entt::entity floorEntity = state->registry.create();
        state->registry.emplace<RenderableComponent>(floorEntity, renderable);
        TransformComponent floorTransform;
        floorTransform.translation = glm::vec3(0.0f, -0.5f, 0.0f);
        floorTransform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        floorTransform.scale = glm::vec3(20.0f, 1.0f, 20.0f);
        state->registry.emplace<TransformComponent>(floorEntity, floorTransform);

        CreateWall(ctx, JPH::RVec3(0, 2.5, -10), JPH::Vec3(10, 2.5, 0.5));
        CreateWall(ctx, JPH::RVec3(0, 2.5, 10), JPH::Vec3(10, 2.5, 0.5));
        CreateWall(ctx, JPH::RVec3(-10, 2.5, 0), JPH::Vec3(0.5, 2.5, 10));
        CreateWall(ctx, JPH::RVec3(10, 2.5, 0), JPH::Vec3(0.5, 2.5, 10));
        SPDLOG_INFO("[DebugSystem] Created physics floor and arena walls");
    }

    if (state->inputFrame->GetKey(Key::F3).pressed) {
        textureHandle = ctx->assetManager->LoadTexture(Platform::GetAssetPath() / "textures/smiling_friend.ktx2");

        for (int i = 0; i < 5; i++) {
            glm::vec3 spawnPos = glm::vec3(i * 2.0f - 4.0f, 5.0f, 0.0f);
            CreateBox(ctx, state, spawnPos);
        }

        SPDLOG_INFO("[DebugSystem] Created falling boxes");
    }


    if (state->inputFrame->GetKey(Key::NUM_8).pressed) {}
    if (state->inputFrame->GetKey(Key::NUM_9).pressed) {}
}
} // Game::System
