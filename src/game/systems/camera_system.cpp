//
// Created by William on 2025-12-21.
//

#include "camera_system.h"

#include "debug_system.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"

namespace Game::System
{
void UpdateCameras(Core::EngineContext* ctx, Engine::GameState* state)
{
    UpdateFreeCamera(ctx, state);
}

void BuildViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily)
{
    auto cameraView = state->registry.view<CameraComponent, MainViewportComponent, TransformComponent>();
    entt::entity mainCamera = cameraView.front();

    const auto& [cam, transform] = cameraView.get(mainCamera);

    mainViewFamily.mainView.currentViewData = cam.currentViewData;
    mainViewFamily.mainView.previousViewData = cam.previousViewData;
    cam.previousViewData = cam.currentViewData;
    mainViewFamily.shadowConfig.cascadeNearPlane = mainViewFamily.mainView.currentViewData.nearPlane;
    mainViewFamily.shadowConfig.cascadeFarPlane = mainViewFamily.mainView.currentViewData.farPlane;

    if (auto* debugView = state->registry.try_get<RenderDebugViewComponent>(mainCamera)) {
        mainViewFamily.mainView.debug = debugView->debugIndex;
    }
}
} // Game
