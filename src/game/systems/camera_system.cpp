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
    mainViewFamily.views.clear();
    mainViewFamily.instances.clear();
    auto cameraView = state->registry.view<CameraComponent, MainViewportComponent, TransformComponent>();


    for (auto entity : cameraView) {
        const auto& [cam, transform] = cameraView.get(entity);

        Core::RenderView view{};

        view.fovRadians = cam.fovRadians;
        view.aspectRatio = cam.aspectRatio;
        view.nearPlane = cam.nearPlane;
        view.farPlane = cam.farPlane;
        view.cameraPos = cam.cameraPos;
        view.cameraLookAt = cam.cameraLookAt;
        view.cameraUp = cam.cameraUp;

        if (auto* debugView = state->registry.try_get<RenderDebugViewComponent>(entity)) {
            view.debug = debugView->debugIndex;
        }

        mainViewFamily.views.push_back(view);
    }
}
} // Game
