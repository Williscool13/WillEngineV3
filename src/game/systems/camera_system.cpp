//
// Created by William on 2025-12-21.
//

#include "camera_system.h"

#include <tracy/Tracy.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "debug_system.h"
#include "probe_bake_system.h"
#include "engine/include/engine_context.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "game/fwd_components.h"
#include "game/gameplay/camera/gameplay_camera.h"
#include "game/input/game_actions.h"

namespace Game
{
void UpdateEditorCamera(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::FreeCameraComponent, Component::CameraComponent, Component::TransformComponent, Component::EditorCameraTag>();
    for (entt::entity entity : view) {
        auto [freeCam, camera, transform] = view.get<Component::FreeCameraComponent, Component::CameraComponent, Component::TransformComponent>(entity);

        glm::vec3 velocity{0.f};
        float verticalVelocity{0.f};
        float yaw = 0;
        float pitch = 0;

        const Core::ActionState& lookModifier = state->input.GetActionState(Actions::ACTION_EDITOR_CAM_LOOK_MODIFIER);
        const bool rmbHeld = lookModifier.down;
        if (!ctx->bImguiMouseCaptured) {
            if (lookModifier.pressed) {
                ctx->setCursorHiddenFn(true);
            } else if (lookModifier.released) {
                ctx->setCursorHiddenFn(false);
            }
        }
        if (!ctx->bImguiKeyboardCaptured && !ctx->bImguiMouseCaptured && rmbHeld) {
            const Core::ActionState& moveAction = state->input.GetActionState(Actions::ACTION_EDITOR_CAM_MOVE);
            velocity.x += moveAction.axis.x;
            velocity.z += moveAction.axis.y;
            if (state->input.GetActionState(Actions::ACTION_EDITOR_CAM_DOWN).down) verticalVelocity -= 1.0f;
            if (state->input.GetActionState(Actions::ACTION_EDITOR_CAM_UP).down) verticalVelocity += 1.0f;
            const Core::ActionState& mouseDelta = state->input.GetActionState(Actions::ACTION_EDITOR_CAM_MOUSE_DELTA);
            yaw = glm::radians(-mouseDelta.axis.x * freeCam.lookSpeed);
            pitch = glm::radians(-mouseDelta.axis.y * freeCam.lookSpeed);
        }

        freeCam.lookSpeed = glm::clamp(freeCam.lookSpeed, 0.1f, 1.0f);
        freeCam.moveSpeed = glm::clamp(freeCam.moveSpeed, 1.0f, 100.0f);

        const float scaledMoveSpeed = state->timeFrame->deltaTime * freeCam.moveSpeed;
        velocity *= scaledMoveSpeed;
        verticalVelocity *= scaledMoveSpeed;

        const glm::quat currentRotation = transform.rotation;
        const glm::vec3 forward = currentRotation * WORLD_FORWARD;
        const float currentPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
        const float newPitch = glm::clamp(currentPitch + pitch, glm::radians(-89.9f), glm::radians(89.9f));
        const float pitchDelta = newPitch - currentPitch;

        const glm::quat yawQuat = glm::angleAxis(yaw, WORLD_UP);
        const glm::quat pitchQuat = glm::angleAxis(pitchDelta, WORLD_RIGHT);
        transform.rotation = glm::normalize(yawQuat * currentRotation * pitchQuat);

        const glm::vec3 right = transform.rotation * WORLD_RIGHT;
        const glm::vec3 forwardDir = transform.rotation * WORLD_FORWARD;
        transform.translation += right * velocity.x + forwardDir * velocity.z + WORLD_UP * verticalVelocity;

        if (!ctx->bImguiMouseCaptured && rmbHeld) {
            const float wheelY = state->input.GetActionState(Actions::ACTION_EDITOR_CAM_ZOOM_SPEED).axis.y;
            freeCam.moveSpeed = glm::clamp(freeCam.moveSpeed + wheelY * 0.5f, 1.0f, 100.0f);
        }

        if (!ctx->bImguiMouseCaptured && state->input.GetActionState(Actions::ACTION_EDITOR_CAM_PAN_MODIFIER).down) {
            const float panScale = scaledMoveSpeed * 0.1f;
            const Core::ActionState& mouseDelta = state->input.GetActionState(Actions::ACTION_EDITOR_CAM_MOUSE_DELTA);
            transform.translation -= right * mouseDelta.axis.x * panScale;
            transform.translation += WORLD_UP * mouseDelta.axis.y * panScale;
        }

        const float aspectRatio = static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight);
        const float fovRadians = glm::radians(state->projectConfig.editorCameraFovDegrees);
        camera.currentViewData = Camera::BuildPerspectiveView(transform.translation, forwardDir, WORLD_UP, aspectRatio, fovRadians, state->projectConfig.editorCameraNearPlane);
    }
}

void BuildViewFamily(Engine::EngineState* state, Core::ViewFamily& mainViewFamily)
{
    ZoneScoped;
    entt::entity mainCamera;
    if (state->inputContext == Engine::InputContext::Gameplay) {
        auto cameraView = state->registry.view<Component::CameraComponent, Component::GameCameraTag, Component::TransformComponent>();
        mainCamera = cameraView.front();
    } else {
        auto cameraView = state->registry.view<Component::CameraComponent, Component::EditorCameraTag, Component::TransformComponent>();
        mainCamera = cameraView.front();
    }

    const auto& [cam, transform] = state->registry.get<Component::CameraComponent, Component::TransformComponent>(mainCamera);

    mainViewFamily.mainView.currentViewData = cam.currentViewData;
    mainViewFamily.mainView.previousViewData = cam.previousViewData;
    cam.previousViewData = cam.currentViewData;

    ProbeBakeOverrideView(state, mainViewFamily);
}

void BuildPortalViewFamily(Engine::EngineState* state, Core::ViewFamily& mainViewFamily)
{
    ZoneScoped;
    entt::entity mainCamera;
    if (state->inputContext == Engine::InputContext::Gameplay) {
        auto cameraView = state->registry.view<Component::CameraComponent, Component::GameCameraTag, Component::TransformComponent>();
        mainCamera = cameraView.front();
    } else {
        auto cameraView = state->registry.view<Component::CameraComponent, Component::EditorCameraTag, Component::TransformComponent>();
        mainCamera = cameraView.front();
    }

    const auto& [cam, cameraTransform] = state->registry.get<Component::CameraComponent, Component::TransformComponent>(mainCamera);

    auto portalView = state->registry.view<Component::PortalComponent, Component::TransformComponent>();

    entt::entity entryPortal = entt::null;
    float closestDistance = std::numeric_limits<float>::max();

    for (const auto& [portalEntity, portal, portalTransform] : portalView.each()) {
        if (portal.linkedPortal == entt::null) continue;

        glm::vec3 toPortal = portalTransform.translation - cameraTransform.translation;
        float distance = glm::length(toPortal);

        glm::vec3 toPortalNorm = toPortal / distance;
        glm::vec3 portalForward = portalTransform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);

        float cameraDot = glm::dot(cam.currentViewData.cameraForward, toPortalNorm);
        float portalDot = glm::dot(portalForward, -toPortalNorm);

        if (cameraDot > 0.0f && portalDot > 0.0f && distance < closestDistance) {
            closestDistance = distance;
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

            glm::mat4 relativeToEntry = glm::inverse(sourceMatrix) * cameraMatrix;
            glm::mat4 flip = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0, 1, 0));
            glm::mat4 portalCameraMatrix = destMatrix * flip * relativeToEntry;

            glm::vec3 portalCameraPos = glm::vec3(portalCameraMatrix[3]);
            glm::vec3 portalForward = -glm::normalize(glm::vec3(portalCameraMatrix[2]));
            glm::vec3 portalUp = glm::normalize(glm::vec3(portalCameraMatrix[1]));
            glm::vec3 portalLookAt = portalCameraPos + portalForward;

            Core::PortalView portalRenderView{};
            portalRenderView.view.currentViewData.fovRadians = cam.currentViewData.fovRadians;
            portalRenderView.view.currentViewData.aspectRatio = cam.currentViewData.aspectRatio;
            portalRenderView.view.currentViewData.nearPlane = cam.currentViewData.nearPlane;
            portalRenderView.view.currentViewData.cameraPos = portalCameraPos;
            portalRenderView.view.currentViewData.cameraLookAt = portalLookAt;
            portalRenderView.view.currentViewData.cameraForward = portalForward;
            portalRenderView.view.currentViewData.cameraUp = portalUp;
            portalRenderView.view.currentViewData.view = glm::lookAt(portalRenderView.view.currentViewData.cameraPos, portalRenderView.view.currentViewData.cameraLookAt,
                                                                     portalRenderView.view.currentViewData.cameraUp);
            portalRenderView.view.currentViewData.proj = glm::infinitePerspective(portalRenderView.view.currentViewData.fovRadians, portalRenderView.view.currentViewData.aspectRatio,
                                                                          portalRenderView.view.currentViewData.nearPlane);

            glm::mat3 entryRotation = glm::mat3(sourceMatrix);
            portalRenderView.entryPortalTransform = Transform(entryTransform.translation, entryTransform.rotation, entryTransform.scale);
            portalRenderView.entryPortalNormal = glm::normalize(entryRotation * glm::vec3(0, 0, 1));
            portalRenderView.entryPortalRight = glm::normalize(entryRotation * glm::vec3(1, 0, 0));
            portalRenderView.entryPortalUp = glm::normalize(entryRotation * glm::vec3(0, 1, 0));
            glm::mat3 exitRotation = glm::mat3(destMatrix);
            portalRenderView.exitPortalTransform = Transform(exitTransform.translation, exitTransform.rotation, exitTransform.scale);
            portalRenderView.exitPortalNormal = glm::normalize(exitRotation * glm::vec3(0, 0, 1));
            portalRenderView.exitPortalRight = glm::normalize(exitRotation * glm::vec3(1, 0, 0));
            portalRenderView.exitPortalUp = glm::normalize(exitRotation * glm::vec3(0, 1, 0));

            DEBUG_ADD_LINE(mainViewFamily.debugLines, Core::DebugLine{
                           .start = exitTransform.translation,
                           .end = exitTransform.translation + portalRenderView.exitPortalNormal * 2.0f,
                           .color = glm::vec4(0, 1, 0, 1)
                           });

            // todo fix this
            portalRenderView.view.previousViewData = portalRenderView.view.currentViewData;

            mainViewFamily.portalViews.PushBack(portalRenderView);
        }
    }
}
} // Game
