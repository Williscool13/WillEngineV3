//
// Created by William on 2025-12-21.
//

#include "camera_system.h"

#include "debug_system.h"
#include "core/include/engine_context.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"

namespace Game::System
{
void UpdateCameras(Core::EngineContext* ctx, Engine::GameState* state)
{
    UpdateFreeCamera(ctx, state);
}

void UpdateFreeCamera(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto view = state->registry.view<Component::FreeCameraComponent, Component::CameraComponent, Component::TransformComponent>();
    for (entt::entity entity : view) {
        const auto& [freeCam, camera, transform] = view.get(entity);

        glm::vec3 velocity{0.f};
        float verticalVelocity{0.f};
        float yaw = 0;
        float pitch = 0;
        if (!ctx->bImguiKeyboardCaptured && !ctx->bImguiMouseCaptured && ctx->windowContext.bCursorHidden) {
            if (state->inputFrame->GetKey(Key::D).down) {
                velocity.x += 1.0f;
            }
            if (state->inputFrame->GetKey(Key::A).down) {
                velocity.x -= 1.0f;
            }
            if (state->inputFrame->GetKey(Key::LCTRL).down) {
                verticalVelocity -= 1.0f;
            }
            if (state->inputFrame->GetKey(Key::SPACE).down) {
                verticalVelocity += 1.0f;
            }
            if (state->inputFrame->GetKey(Key::W).down) {
                velocity.z += 1.0f;
            }
            if (state->inputFrame->GetKey(Key::S).down) {
                velocity.z -= 1.0f;
            }

            if (state->inputFrame->GetKey(Key::MINUS).down) {
                freeCam.moveSpeed += 0.1f;
            }
            if (state->inputFrame->GetKey(Key::EQUALS).down) {
                freeCam.lookSpeed -= 0.1f;
            }
            if (state->inputFrame->GetKey(Key::RIGHTBRACKET).down) {
                freeCam.moveSpeed += 1;
            }
            if (state->inputFrame->GetKey(Key::LEFTBRACKET).down) {
                freeCam.moveSpeed -= 1;
            }
            yaw = glm::radians(-state->inputFrame->mouseXDelta * freeCam.lookSpeed);
            pitch = glm::radians(-state->inputFrame->mouseYDelta * freeCam.lookSpeed);
        }


        freeCam.lookSpeed = glm::clamp(freeCam.lookSpeed, 0.1f, 1.0f);
        freeCam.moveSpeed = glm::clamp(freeCam.moveSpeed, 1.0f, 100.0f);

        const float scaledMoveSpeed = state->timeFrame->deltaTime * freeCam.moveSpeed;
        velocity *= scaledMoveSpeed;
        verticalVelocity *= scaledMoveSpeed;

        const glm::quat currentRotation = transform.rotation;
        const glm::vec3 forward = currentRotation * WORLD_FORWARD;
        const float currentPitch = std::asin(forward.y);

        const float newPitch = glm::clamp(currentPitch + pitch, glm::radians(-89.9f), glm::radians(89.9f));
        const float pitchDelta = newPitch - currentPitch;

        const glm::quat yawQuat = glm::angleAxis(yaw, WORLD_UP);
        const glm::quat pitchQuat = glm::angleAxis(pitchDelta, WORLD_RIGHT);

        transform.rotation = glm::normalize(yawQuat * currentRotation * pitchQuat);

        const glm::vec3 right = transform.rotation * WORLD_RIGHT;
        const glm::vec3 forwardDir = transform.rotation * WORLD_FORWARD;

        transform.translation += right * velocity.x + forwardDir * velocity.z + WORLD_UP * verticalVelocity;

        camera.currentViewData.cameraPos = transform.translation;
        camera.currentViewData.cameraLookAt = transform.translation + forwardDir;
        camera.currentViewData.cameraForward = forwardDir;
        camera.currentViewData.cameraUp = WORLD_UP;
        camera.currentViewData.aspectRatio = static_cast<float>(ctx->windowContext.windowWidth) / static_cast<float>(ctx->windowContext.windowHeight);
        camera.currentViewData.fovRadians = glm::radians(70.0f);
        camera.currentViewData.nearPlane = 0.1f;
        camera.currentViewData.farPlane = 100.0f;
    }
}

void BuildViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily)
{
    auto cameraView = state->registry.view<Component::CameraComponent, Component::MainViewportComponent, Component::TransformComponent>();
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
    auto cameraView = state->registry.view<Component::CameraComponent, Component::MainViewportComponent, Component::TransformComponent>();
    entt::entity mainCamera = cameraView.front();
    const auto& [cam, cameraTransform] = cameraView.get(mainCamera);

    auto portalView = state->registry.view<Component::PortalComponent, Component::TransformComponent>();

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
        const auto& entryPortalComp = state->registry.get<Component::PortalComponent>(entryPortal);
        const auto& entryTransform = state->registry.get<Component::TransformComponent>(entryPortal);

        if (state->registry.valid(entryPortalComp.linkedPortal)) {
            const auto& exitTransform = state->registry.get<Component::TransformComponent>(entryPortalComp.linkedPortal);

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
