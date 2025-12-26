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
#include "game/components/gameplay/anti_gravity_component.h"
#include "game/components/gameplay/floor_component.h"
#include "game/components/physics/dynamic_physics_body_component.h"
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
    boxShapeSettings.SetDensity(12.5f);
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
    state->registry.emplace<PhysicsBodyComponent>(boxEntity, boxBodyID);
    state->registry.emplace<DynamicPhysicsBodyComponent>(boxEntity, transformComponent.translation, transformComponent.rotation);
}

entt::entity CreateStaticBox(Core::EngineContext* ctx, Engine::GameState* state,
                             JPH::RVec3 physicsPos, JPH::Vec3 halfExtents,
                             glm::vec3 renderPos, glm::vec3 renderScale,
                             glm::vec4 color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f))
{
    if (!boxHandle.IsValid()) {
        SPDLOG_WARN("[CreateStaticBox] No box model loaded");
        return entt::null;
    }

    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    // Create physics body
    JPH::BoxShapeSettings shapeSettings(halfExtents);
    shapeSettings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
    JPH::ShapeRefC shape = shapeResult.Get();

    JPH::BodyCreationSettings bodySettings(
        shape,
        physicsPos,
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Physics::Layers::NON_MOVING
    );

    JPH::BodyID bodyID = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);

    // Create renderable
    Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
    if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
        SPDLOG_WARN("[CreateStaticBox] Model not ready yet");
        return entt::null;
    }

    auto& modelAllocator = ctx->assetManager->GetModelAllocator();
    auto& instanceAllocator = ctx->assetManager->GetInstanceAllocator();
    auto& materialAllocator = ctx->assetManager->GetMaterialAllocator();

    Engine::ModelHandle modelEntry = modelAllocator.Add();
    Engine::InstanceHandle instanceEntry = instanceAllocator.Add();
    Engine::MaterialHandle materialEntry = materialAllocator.Add();

    if (!modelEntry.IsValid() || !instanceEntry.IsValid() || !materialEntry.IsValid()) {
        SPDLOG_ERROR("[CreateStaticBox] Failed to allocate GPU slots");
        modelAllocator.Remove(modelEntry);
        instanceAllocator.Remove(instanceEntry);
        materialAllocator.Remove(materialEntry);
        return entt::null;
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
        renderable.material.colorFactor = color;
        renderable.material.metalRoughFactors = {0.0f, 1.0f, 0.0f, 0.0f};
    }
    else {
        renderable.material = model->modelData.materials[0];
        renderable.material.textureImageIndices.x = AssetLoad::ERROR_IMAGE_BINDLESS_INDEX;
        renderable.material.textureSamplerIndices.x = AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX;
    }

    // Create entity
    entt::entity entity = state->registry.create();
    state->registry.emplace<RenderableComponent>(entity, renderable);

    TransformComponent transform;
    transform.translation = renderPos;
    transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    transform.scale = renderScale;
    state->registry.emplace<TransformComponent>(entity, transform);
    state->registry.emplace<PhysicsBodyComponent>(entity, bodyID);

    return entity;
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
        entt::entity floor = CreateStaticBox(
            ctx, state,
            JPH::RVec3(0.0, -0.5, 0.0), JPH::Vec3(10.0f, 0.5f, 10.0f), // physics
            glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f), // render
            glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) // gray
        );
        state->registry.emplace<FloorComponent>(floor);

        // Create walls
        CreateStaticBox(ctx, state, JPH::RVec3(0, 2.5, -10), JPH::Vec3(10, 2.5, 0.5),
                        glm::vec3(0, 2.5, -10), glm::vec3(20, 5, 1)); // back
        CreateStaticBox(ctx, state, JPH::RVec3(0, 2.5, 10), JPH::Vec3(10, 2.5, 0.5),
                        glm::vec3(0, 2.5, 10), glm::vec3(20, 5, 1)); // front
        CreateStaticBox(ctx, state, JPH::RVec3(-10, 2.5, 0), JPH::Vec3(0.5, 2.5, 10),
                        glm::vec3(-10, 2.5, 0), glm::vec3(1, 5, 20)); // left
        CreateStaticBox(ctx, state, JPH::RVec3(10, 2.5, 0), JPH::Vec3(0.5, 2.5, 10),
                        glm::vec3(10, 2.5, 0), glm::vec3(1, 5, 20)); // right

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

void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::GameState* state)
{
    std::span<const Physics::DeferredCollisionEvent> events = ctx->physicsSystem->GetCollisionEvents();


    state->registry.clear<AntiGravityComponent>();

    for (const auto& event : events) {
        entt::entity entity1 = state->bodyToEntity.contains(event.body1)
                                   ? state->bodyToEntity[event.body1]
                                   : entt::null;
        entt::entity entity2 = state->bodyToEntity.contains(event.body2)
                                   ? state->bodyToEntity[event.body2]
                                   : entt::null;

        if (entity1 == entt::null || entity2 == entt::null) continue;

        if (state->registry.all_of<FloorComponent>(entity2)) {
            state->registry.emplace_or_replace<AntiGravityComponent>(entity1);
        }
        else if (state->registry.all_of<FloorComponent>(entity1)) {
            state->registry.emplace_or_replace<AntiGravityComponent>(entity2);
        }
    }

    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
}

void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto view = state->registry.view<AntiGravityComponent, PhysicsBodyComponent>();
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    for (auto [entity, physics] : view.each()) {
        bodyInterface.AddImpulse(physics.bodyID, JPH::Vec3(0, 100.0f, 0));
    }
}
} // Game::System
