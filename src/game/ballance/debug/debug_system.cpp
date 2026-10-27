//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include <tracy/Tracy.hpp>

#include "audio/audio_manager.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/debug_gizmo_component.h"
#include "engine/components/physics/physics_body_component.h"
#include "game/ballance/debug/debug_components.h"
#include "game/ballance/fwd_components.h"
#include "game/input/game_actions.h"
#include "physics/physics_world.h"
#include "platform/paths.h"

namespace Ballance
{
void DebugUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->input.GetActionState(Game::Actions::ACTION_DEBUG_PLAY_MUSIC).pressed) {
        auto musicPath = Platform::GetAssetPath() / "audio/the_entertainer.ogg";
        auto _musicPath = Core::Path(musicPath);
        Core::Handle<Audio::WillAudio> testMusic = ctx->audioManager->LoadAudio("test_music", _musicPath);
        ctx->audioManager->PlayMusic(testMusic);
    }
    if (state->input.GetActionState(Game::Actions::ACTION_DEBUG_MUSIC_VOL_LOW).pressed) {
        ctx->audioManager->SetMusicVolume(0.25f);
    }
    if (state->input.GetActionState(Game::Actions::ACTION_DEBUG_MUSIC_VOL_FULL).pressed) {
        ctx->audioManager->SetMusicVolume(1.0f);
    }
}

void DebugProcessPhysicsCollisions(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    state->registry.Clear<Component::AntiGravityTag>();

    for (const auto& event : state->physics.resolvedAddedEvents) {
        entt::entity e1 = event.e1;
        entt::entity e2 = event.e2;
        if (e1 == entt::null || e2 == entt::null) continue;

        if (state->registry.Has<Component::FloorTag>(e2))
            state->registry.Set<Component::AntiGravityTag>(e1);
        else if (state->registry.Has<Component::FloorTag>(e1))
            state->registry.Set<Component::AntiGravityTag>(e2);
    }


}

void DebugApplyGroundForces(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.View<Component::AntiGravityTag, Component::PhysicsBodyRuntime>();
    for (const auto& [entity, body] : view.each()) {
        ctx->physicsWorld->AddImpulse(body.bodyID, Vec3(0, 100.0f, 0));
    }
}

#ifdef WDEBUG
void DebugRender(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    auto& vf = frameBuffer->mainViewFamily;
    auto view = state->registry.View<Component::DebugGizmoComponent, Component::TransformComponent>();
    for (auto entity : view) {
        auto& gizmo = view.get<Component::DebugGizmoComponent>(entity);
        auto& transform = view.get<Component::TransformComponent>(entity);

        switch (gizmo.shape) {
        case Component::DebugGizmoShape::Sphere:
            DEBUG_ADD_SPHERE(vf.debugSpheres, Core::DebugSphere{
                .center = transform.translation,
                .radius = gizmo.extents.x,
                .color = gizmo.color,
                .width = gizmo.lineWidth,
            });
            break;
        case Component::DebugGizmoShape::Box:
            DEBUG_ADD_BOX(vf.debugBoxes, Core::DebugBox{
                .center = transform.translation,
                .extents = gizmo.extents,
                .rotation = transform.rotation,
                .color = gizmo.color,
                .width = gizmo.lineWidth,
            });
            break;
        default:
            break;
        }
    }
}
#endif
} // Ballance
