//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include "game/fwd_components.h"
#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "platform/paths.h"

namespace Game::System
{
static Engine::WillModelHandle boxHandle = Engine::WillModelHandle::INVALID;
static entt::entity boxEntity = entt::null;

void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    bool static resetted = false;
    if (!resetted) {
        boxHandle = Engine::WillModelHandle::INVALID;
        resetted = true;
        boxEntity = entt::null;
    }

    if (state->inputFrame->GetKey(Key::F1).pressed) {
        if (!boxHandle.IsValid()) {
            boxHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "BoxTextured.willmodel");
        }
    }
    if (state->inputFrame->GetKey(Key::F2).pressed) {
        if (boxHandle.IsValid()) {
            ctx->assetManager->UnloadModel(boxHandle);
        }
    }

    if (state->inputFrame->GetKey(Key::F3).pressed) {
        if (!boxHandle.IsValid()) {
            SPDLOG_WARN("[DebugSystem] No model loaded, press F1 first");
            return;
        }

        Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);
        if (!model || model->modelLoadState != Render::WillModel::ModelLoadState::Loaded) {
            SPDLOG_WARN("[DebugSystem] Model not ready yet");
            return;
        }

        if (state->registry.valid(boxEntity)) {
            SPDLOG_WARN("[DebugSystem] Box already spawned");
            return;
        }

        auto& modelAllocator = ctx->assetManager->GetModelAllocator();
        auto& instanceAllocator = ctx->assetManager->GetInstanceAllocator();
        auto& materialAllocator = ctx->assetManager->GetMaterialAllocator();

        Engine::ModelHandle modelEntry = modelAllocator.Add();
        Engine::InstanceHandle instanceEntry = instanceAllocator.Add();
        Engine::MaterialHandle materialEntry = materialAllocator.Add();

        if (modelEntry.index == Core::INVALID_HANDLE_INDEX || instanceEntry.index == Core::INVALID_HANDLE_INDEX || materialEntry.index == Core::INVALID_HANDLE_INDEX) {
            SPDLOG_ERROR("[DebugSystem] Failed to allocate GPU slots");
            return;
        }

        RenderableComponent renderable;
        renderable.modelEntry = modelEntry;
        renderable.instanceEntry = instanceEntry;
        renderable.materialEntry = materialEntry;

        renderable.modelFlags = glm::vec4(0.0f);

        renderable.instance.primitiveIndex = model->modelData.meshes[0].primitiveIndices[0].index;
        renderable.instance.modelIndex = modelEntry.index;
        renderable.instance.materialIndex = materialEntry.index;
        renderable.instance.jointMatrixOffset = 0;
        renderable.instance.bIsAllocated = 1;

        renderable.material.colorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        renderable.material.metalRoughFactors = {0.0f, 1.0f, 0.0f, 0.0f};
        renderable.material = model->modelData.materials[0];
        // todo: default materials

        Transform transform{Transform::IDENTITY};
        transform.translation = glm::vec3(0.0f);

        boxEntity = state->registry.create();
        state->registry.emplace<RenderableComponent>(boxEntity, renderable);
        state->registry.emplace<TransformComponent>(boxEntity, transform);

        SPDLOG_INFO("[DebugSystem] Spawned box entity");
    }

    if (state->registry.valid(boxEntity)) {
        auto& transform = state->registry.get<TransformComponent>(boxEntity);

        float radius = 5.0f;
        float speed = 2.0f;
        transform.transform.translation.x = glm::cos(state->timeFrame->totalTime * speed) * radius;
        transform.transform.translation.z = glm::sin(state->timeFrame->totalTime * speed) * radius;
        transform.transform.translation.y = 0.0f;
    }
}

void DebugPrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer, Render::FrameResources* frameResources)
{
    Instance* instanceBuffer = static_cast<Instance*>(frameResources->instanceBuffer.allocationInfo.pMappedData);
    Model* modelBuffer = static_cast<Model*>(frameResources->modelBuffer.allocationInfo.pMappedData);
    MaterialProperties* materialBuffer = static_cast<MaterialProperties*>(frameResources->materialBuffer.allocationInfo.pMappedData);

    auto view = state->registry.view<RenderableComponent, TransformComponent>();

    for (auto [entity, renderable, transform] : view.each()) {
        // todo: prev frame data needs to be figured out
        modelBuffer[renderable.modelEntry.index] = {transform.transform.GetMatrix(), transform.transform.GetMatrix(), renderable.modelFlags};
        instanceBuffer[renderable.instanceEntry.index] = renderable.instance;
        materialBuffer[renderable.materialEntry.index] = renderable.material;

        frameBuffer->mainViewFamily.instances.push_back(renderable.instance);
    }
}
} // Game::System
