//
// Created by William on 2025-12-21.
//

#include "camera_system.h"

#include <tracy/Tracy.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/editor/probe_bake_system.h"
#include "engine/include/engine_context.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "engine/components/fwd_components.h"
#include "engine/input/engine_actions.h"

namespace Engine
{
Core::ViewData BuildPerspectiveView(glm::vec3 pos, glm::vec3 forward, glm::vec3 up, float aspectRatio, float fovRadians, float nearPlane)
{
    const glm::vec3 f = glm::normalize(forward);

    Core::ViewData vd{};
    vd.cameraPos = pos;
    vd.cameraForward = f;
    vd.cameraUp = up;
    vd.cameraLookAt = pos + f;
    vd.aspectRatio = aspectRatio;
    vd.fovRadians = fovRadians;
    vd.nearPlane = nearPlane;
    vd.view = glm::lookAt(pos, vd.cameraLookAt, up);

    const float tanHalfFov = glm::tan(fovRadians * 0.5f);
    // Reverse-Z, infinite far: ndc.z = near/dist (1 at near, ->0 at infinity).
    // Hand-rolled rather than glm::infinitePerspective(fovRadians, aspectRatio, nearPlane), which isn't reverse-Z.
    glm::mat4 proj(0.0f);
    proj[0][0] = 1.0f / (aspectRatio * tanHalfFov);
    proj[1][1] = 1.0f / tanHalfFov;
    proj[2][3] = -1.0f;
    proj[3][2] = nearPlane;
    vd.proj = proj;
    return vd;
}

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
        camera.currentViewData = BuildPerspectiveView(transform.translation, forwardDir, WORLD_UP, aspectRatio, fovRadians, state->projectConfig.editorCameraNearPlane);
    }
}

bool BuildViewFamily(Engine::EngineContext* ctx, Engine::EngineState* state, Core::ViewFamily& mainViewFamily)
{
    ZoneScoped;
    entt::entity mainCamera;
#if WILL_EDITOR
    if (state->inputContext == Engine::InputContext::Gameplay) {
        auto cameraView = state->registry.view<Component::CameraComponent, Component::GameCameraTag, Component::TransformComponent>();
        mainCamera = cameraView.front();
    } else {
        auto cameraView = state->registry.view<Component::CameraComponent, Component::EditorCameraTag, Component::TransformComponent>();
        mainCamera = cameraView.front();
    }
#else
    auto cameraView = state->registry.view<Component::CameraComponent, Component::GameCameraTag, Component::TransformComponent>();
    mainCamera = cameraView.front();
#endif

    auto& cam = state->registry.get<Component::CameraComponent>(mainCamera);

    if (mainCamera != state->renderedCamera && state->registry.valid(state->renderedCamera)) {
        if (const auto* renderedCam = state->registry.try_get<Component::CameraComponent>(state->renderedCamera)) {
            cam.previousViewData = renderedCam->previousViewData;
        }
    }
    state->renderedCamera = mainCamera;

    const bool bCut = cam.transition == Component::CameraTransition::Cut;
    if (bCut) {
        cam.previousViewData = cam.currentViewData;
        cam.transition = Component::CameraTransition::Continuous;
    }
    mainViewFamily.mainView.currentViewData = cam.currentViewData;
    mainViewFamily.mainView.previousViewData = cam.previousViewData;
    cam.previousViewData = cam.currentViewData;

#ifdef WDEBUG
    const int32_t profileSlot = state->debug.profileCameraSlot;
#if WILL_EDITOR
    const bool bGameView = state->inputContext == Engine::InputContext::Gameplay;
#else
    const bool bGameView = true;
#endif
    if (bGameView && profileSlot >= 0 && state->projectConfig.cameraPresets[profileSlot].bSet) {
        const CameraPreset& preset = state->projectConfig.cameraPresets[profileSlot];
        const float aspect = state->projectConfig.ResolvedGameAspect(static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight));
        mainViewFamily.mainView.currentViewData = BuildPerspectiveView(preset.translation, preset.rotation * WORLD_FORWARD, WORLD_UP, aspect,
                                                                       glm::radians(state->projectConfig.gameCameraFovDegrees), state->projectConfig.gameCameraNearPlane);
        mainViewFamily.mainView.previousViewData = mainViewFamily.mainView.currentViewData;
    }
#endif

    ProbeBakeOverrideView(state, mainViewFamily);
    return bCut;
}

} // Engine
