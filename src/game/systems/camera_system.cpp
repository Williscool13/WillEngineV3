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
}

void BuildPortalViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily)
{
    auto cameraView = state->registry.view<CameraComponent, MainViewportComponent, TransformComponent>();
    entt::entity mainCamera = cameraView.front();
    const auto& [cam, transform] = cameraView.get(mainCamera);

    Core::RenderView portalView{};
    portalView.currentViewData.fovRadians = cam.currentViewData.fovRadians;
    portalView.currentViewData.aspectRatio = cam.currentViewData.aspectRatio;
    portalView.currentViewData.nearPlane = cam.currentViewData.nearPlane;
    portalView.currentViewData.farPlane = cam.currentViewData.farPlane;
    portalView.currentViewData.cameraPos = glm::vec3(0.0f, 0.0f, 0.0f);
    portalView.currentViewData.cameraLookAt = glm::vec3(0.0f, 0.0f, -1.0f);
    portalView.currentViewData.cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
    portalView.currentViewData.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    portalView.previousViewData = portalView.currentViewData;
    mainViewFamily.portalViews.push_back(portalView);
}
} // Game
