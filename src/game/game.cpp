//
// Created by William on 2025-12-14.
//

#include "spdlog/spdlog.h"

#include "core/include/game_interface.h"
#include "core/include/render_interface.h"
#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"
#include "fwd_components.h"
#include "imgui.h"
#include "audio/audio_manager.h"
#include "components/gameplay/anti_gravity_component.h"
#include "components/gameplay/floor_component.h"
#include "components/gameplay/portals/portal_component.h"
#include "components/physics/dynamic_physics_body_component.h"
#include "systems/gather_renderables_system.h"
#include "components/render/portal_plane_component.h"
#include "core/math/constants.h"
#include "systems/debug_system.h"
#include "systems/camera_system.h"
#include "systems/editor_systems.h"
#include "systems/physics_system.h"


extern "C"
{
GAME_API void GameStartup(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Start Up");

    const entt::entity camera = state->registry.create();
    state->registry.emplace<Game::FreeCameraComponent>(camera);
    state->registry.emplace<Game::CameraComponent>(camera);
    Game::TransformComponent& cameraTransform = state->registry.emplace<Game::TransformComponent>(camera);
    cameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    cameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);
    state->registry.emplace<Game::MainViewportComponent>(camera);
    state->registry.ctx().emplace<Engine::GameState*>(state);

    spdlog::set_default_logger(ctx->logger);
}

GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("[Game] Registering engine component types:");
    SPDLOG_TRACE("  TransformComponent: {}", entt::type_id<Game::TransformComponent>().hash());
    SPDLOG_TRACE("  CameraComponent: {}", entt::type_id<Game::CameraComponent>().hash());
    SPDLOG_TRACE("  MainViewportComponent: {}", entt::type_id<Game::MainViewportComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::FreeCameraComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::PortalPlaneComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::PortalComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::AntiGravityComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::FloorComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::DynamicPhysicsBodyComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::PhysicsBodyComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::DirtyPhysicsTransformComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::RenderableComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::TransformComponent>().hash());

    spdlog::set_default_logger(ctx->logger);
    ImGui::SetCurrentContext(ctx->imguiContext);

    ctx->physicsSystem->RegisterAllocator();
    ctx->audioManager->RegisterAudio();
    state->registry.on_construct<Game::PhysicsBodyComponent>().connect<&Game::OnPhysicsBodyAdded>();
    state->registry.on_destroy<Game::PhysicsBodyComponent>().connect<&Game::OnPhysicsBodyRemoved>();
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    Game::System::UpdateCameras(ctx, state);
    Game::System::DebugUpdate(ctx, state);

    Game::System::DebugProcessPhysicsCollisions(ctx, state);
    Game::System::DebugApplyGroundForces(ctx, state);

    for (const auto& hotkey : Game::DEBUG_HOTKEYS) {
        if (state->inputFrame->GetKey(hotkey.key).pressed) {
            if (state->debugResourceName == hotkey.resourceName && state->debugViewAspect == hotkey.aspect) {
                state->debugResourceName.clear();
            }
            else {
                state->debugResourceName = hotkey.resourceName;
                state->debugTransformationType = hotkey.transform;
                state->debugViewAspect = hotkey.aspect;
            }
        }
    }

    if (state->bEnablePhysics) {
        Game::System::UpdatePhysics(ctx, state);
    }

    Core::InputFrame gameInputCopy = *state->inputFrame;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->mainViewFamily.modelMatrices.clear();
    frameBuffer->mainViewFamily.mainInstances.clear();
    for (Core::CustomStencilDrawBatch& customStencilBatch : frameBuffer->mainViewFamily.customStencilDraws) {
        customStencilBatch.instances.clear();
    }
    frameBuffer->mainViewFamily.materials.clear();
    frameBuffer->mainViewFamily.portalViews.clear();
#ifndef PACKAGED_BUILD
    frameBuffer->mainViewFamily.debugLines.clear();
    frameBuffer->mainViewFamily.debugBoxes.clear();
    frameBuffer->mainViewFamily.debugSpheres.clear();
#endif

    Game::System::BuildViewFamily(state, frameBuffer->mainViewFamily);
    if (state->bEnablePortal) {
        Game::System::BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }
    Game::System::GatherRenderables(ctx, state, frameBuffer);

#if WILL_EDITOR
    Game::System::DrawEditorInterface(ctx, state, frameBuffer);
#endif

#ifndef PACKAGED_BUILD
    glm::vec3 debugOffset(-30, 0, 0);

    // Coordinate axes
    frameBuffer->mainViewFamily.debugLines.push_back({
        .start = debugOffset + glm::vec3{0.1f, 0.1f, 0.1f},
        .end = debugOffset + glm::vec3(5, 0, 0) + glm::vec3{0.1f, 0.1f, 0.1f},
        .color = glm::vec4(1, 0, 0, 1)
    });
    frameBuffer->mainViewFamily.debugLines.push_back({
        .start = debugOffset + glm::vec3{0.1f, 0.1f, 0.1f},
        .end = debugOffset + glm::vec3(0, 5, 0) + glm::vec3{0.1f, 0.1f, 0.1f},
        .color = glm::vec4(0, 1, 0, 1)
    });
    frameBuffer->mainViewFamily.debugLines.push_back({
        .start = debugOffset + glm::vec3{0.1f, 0.1f, 0.1f},
        .end = debugOffset + glm::vec3(0, 0, 5) + glm::vec3{0.1f, 0.1f, 0.1f},
        .color = glm::vec4(0, 0, 1, 1)
    });

    // Grid on XZ plane
    for (int i = -5; i <= 5; ++i) {
        frameBuffer->mainViewFamily.debugLines.push_back({
            .start = debugOffset + glm::vec3(i * 2.0f, 0, -10),
            .end = debugOffset + glm::vec3(i * 2.0f, 0, 10),
            .color = glm::vec4(0.3f, 0.3f, 0.3f, 1)
        });
        frameBuffer->mainViewFamily.debugLines.push_back({
            .start = debugOffset + glm::vec3(-10, 0, i * 2.0f),
            .end = debugOffset + glm::vec3(10, 0, i * 2.0f),
            .color = glm::vec4(0.3f, 0.3f, 0.3f, 1)
        });
    }

    // Rotated box
    frameBuffer->mainViewFamily.debugBoxes.push_back({
        .center = debugOffset + glm::vec3(10, 2, 0),
        .extents = glm::vec3(1, 1, 1),
        .rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0)),
        .color = glm::vec4(1, 1, 0, 0.5f)
    });

    // Stack of boxes
    for (int i = 0; i < 3; ++i) {
        frameBuffer->mainViewFamily.debugBoxes.push_back({
            .center = debugOffset + glm::vec3(0, 1.0f + i * 2.5f, 5),
            .extents = glm::vec3(0.8f, 1.0f, 0.8f),
            .rotation = glm::angleAxis(glm::radians(i * 30.0f), glm::vec3(0, 1, 0)),
            .color = glm::vec4(0.2f + i * 0.3f, 0.5f, 1.0f, 0.6f)
        });
    }

    // Sphere at different heights
    for (int i = 0; i < 4; ++i) {
        frameBuffer->mainViewFamily.debugSpheres.push_back({
            .center = debugOffset + glm::vec3(-10, 1.5f + i * 3.0f, 0),
            .radius = 0.5f + i * 0.3f,
            .color = glm::vec4(1, 0.2f * i, 1 - 0.2f * i, 0.5f)
        });
    }

    // Orbital path visualization
    constexpr int orbitSegments = 16;
    glm::vec3 orbitCenter = debugOffset + glm::vec3(0, 5, -5);
    float orbitRadius = 4.0f;
    for (int i = 0; i < orbitSegments; ++i) {
        float angle1 = (float) i / orbitSegments * 2.0f * glm::pi<float>();
        float angle2 = (float) (i + 1) / orbitSegments * 2.0f * glm::pi<float>();

        glm::vec3 p1 = orbitCenter + glm::vec3(glm::cos(angle1) * orbitRadius, 0, glm::sin(angle1) * orbitRadius);
        glm::vec3 p2 = orbitCenter + glm::vec3(glm::cos(angle2) * orbitRadius, 0, glm::sin(angle2) * orbitRadius);

        frameBuffer->mainViewFamily.debugLines.push_back({
            .start = p1,
            .end = p2,
            .color = glm::vec4(0, 1, 1, 1)
        });
    }

    // Bounding volume hierarchy visualization
    frameBuffer->mainViewFamily.debugSpheres.push_back({
        .center = orbitCenter,
        .radius = orbitRadius + 0.5f,
        .color = glm::vec4(1, 0.5f, 0, 0.3f)
    });
#endif
    ImGui::End();
}

GAME_API void GameUnload(Core::EngineContext* ctx, Engine::GameState* state)
{
    state->registry.on_construct<Game::PhysicsBodyComponent>().disconnect();
    state->registry.on_destroy<Game::PhysicsBodyComponent>().disconnect();
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
