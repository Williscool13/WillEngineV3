//
// Created by William on 2025-12-21.
//

#include "camera_system.h"

#include "debug_system.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"
#include "game/components/gameplay/portals/portal_component.h"
#include "spdlog/spdlog.h"

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
    const auto& [cam, cameraTransform] = cameraView.get(mainCamera);

    auto portalView = state->registry.view<PortalComponent, TransformComponent>();

    entt::entity entryPortal = entt::null;
    float bestDot = -1.0f;

    for (auto [portalEntity, portal, portalTransform] : portalView.each()) {
        if (portal.linkedPortal == entt::null) continue;

        glm::vec3 toPortal = glm::normalize(portalTransform.translation - cameraTransform.translation);

        glm::vec3 portalForward = portalTransform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);

        float cameraDot = glm::dot(cam.currentViewData.cameraForward, toPortal);
        float portalDot = glm::dot(portalForward, -toPortal);

        if (cameraDot > 0.0f && portalDot > 0.0f && cameraDot > bestDot) {
            bestDot = cameraDot;
            entryPortal = portalEntity;
        }
    }

    if (entryPortal != entt::null) {
        const auto& entryPortalComp = state->registry.get<PortalComponent>(entryPortal);
        const auto& entryTransform = state->registry.get<TransformComponent>(entryPortal);

        if (state->registry.valid(entryPortalComp.linkedPortal)) {
            const auto& exitTransform = state->registry.get<TransformComponent>(entryPortalComp.linkedPortal);

            glm::mat4 sourceMatrix = GetMatrix(entryTransform);
            glm::mat4 destMatrix = GetMatrix(exitTransform);
            glm::mat4 cameraMatrix = GetMatrix(cameraTransform);

            glm::mat4 portalCameraMatrix = destMatrix * glm::inverse(sourceMatrix) * cameraMatrix;

            glm::vec3 portalCameraPos = glm::vec3(portalCameraMatrix[3]);
            glm::vec3 portalForward = -glm::normalize(glm::vec3(portalCameraMatrix[2]));
            glm::vec3 portalUp = glm::normalize(glm::vec3(portalCameraMatrix[1]));
            glm::vec3 portalLookAt = portalCameraPos + portalForward;

            Core::RenderView portalRenderView{};
            portalRenderView.currentViewData.fovRadians = cam.currentViewData.fovRadians;
            portalRenderView.currentViewData.aspectRatio = cam.currentViewData.aspectRatio;
            portalRenderView.currentViewData.nearPlane = cam.currentViewData.nearPlane;
            portalRenderView.currentViewData.farPlane = cam.currentViewData.farPlane;
            portalRenderView.currentViewData.cameraPos = portalCameraPos;
            portalRenderView.currentViewData.cameraLookAt = portalLookAt;
            portalRenderView.currentViewData.cameraForward = portalForward;
            portalRenderView.currentViewData.cameraUp = portalUp;

            portalRenderView.previousViewData = portalRenderView.currentViewData;

            mainViewFamily.portalViews.push_back(portalRenderView);
        }
    }
}
} // Game
