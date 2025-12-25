//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include <Jolt/Jolt.h>
#include <spdlog/spdlog.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"
#include "render/frame_resources.h"
#include "physics/physics_system.h"
#include "platform/paths.h"


namespace Game::System
{
static Engine::WillModelHandle dragonHandle = Engine::WillModelHandle::INVALID;
static Engine::WillModelHandle boxHandle = Engine::WillModelHandle::INVALID;
static Engine::TextureHandle textureHandle = Engine::TextureHandle::INVALID;
static JPH::BodyID boxBodyID;
static JPH::BodyID floorBodyID;

void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (state->inputFrame->GetKey(Key::F1).pressed) {
        if (!dragonHandle.IsValid()) {
            dragonHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "dragon/dragon.willmodel");
            boxHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "BoxTextured.willmodel");
        }
    }

    if (state->inputFrame->GetKey(Key::F2).pressed) {
        // We do not insert floor into registry, though maybe we should
        auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();
        SPDLOG_INFO("[DebugSystem] Created physics floor");
    }

    if (state->inputFrame->GetKey(Key::F3).pressed) {
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
            JPH::RVec3(0.0, 5.0, 0.0),
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
            // renderable.material.textureImageIndices.x = 1;
        }

        Transform transform{};
        transform.translation = glm::vec3(0.0f, 5.0f, 0.0f);

        entt::entity boxEntity = state->registry.create();
        state->registry.emplace<RenderableComponent>(boxEntity, renderable);
        state->registry.emplace<TransformComponent>(boxEntity, transform);
        state->registry.emplace<PhysicsBodyComponent>(boxEntity, boxBodyID, transform.translation, transform.rotation);


        SPDLOG_INFO("[DebugSystem] Created falling box");
    }


    if (state->inputFrame->GetKey(Key::NUM_8).pressed) {
        textureHandle = ctx->assetManager->LoadTexture(Platform::GetAssetPath() / "textures/smiling_friend.ktx2");
    }
    if (state->inputFrame->GetKey(Key::NUM_9).pressed) {
        ctx->assetManager->UnloadTexture(textureHandle);
    }

    // todo: if box is no longer moving, lets move it again!

}

void DebugPrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer, Render::FrameResources* frameResources)
{
    auto* instanceBuffer = static_cast<Instance*>(frameResources->instanceBuffer.allocationInfo.pMappedData);
    auto modelBuffer = static_cast<Model*>(frameResources->modelBuffer.allocationInfo.pMappedData);
    auto materialBuffer = static_cast<MaterialProperties*>(frameResources->materialBuffer.allocationInfo.pMappedData);

    const auto view = state->registry.view<RenderableComponent, TransformComponent>();

    for (const auto& [entity, renderable, transform] : view.each()) {
        // todo: prev frame data needs to be figured out
        modelBuffer[renderable.modelEntry.index] = {transform.transform.GetMatrix(), transform.transform.GetMatrix(), renderable.modelFlags};
        instanceBuffer[renderable.instanceEntry.index] = renderable.instance;
        materialBuffer[renderable.materialEntry.index] = renderable.material;

        frameBuffer->mainViewFamily.instances.push_back(renderable.instanceEntry);
    }
}

void DebugShutdown(Core::EngineContext* ctx, Engine::GameState* state)
{}
} // Game::System
