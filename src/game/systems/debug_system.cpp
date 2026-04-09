//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include <Jolt/Jolt.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_manager.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/components/common_components.h"
#include "game/components/core_components.h"
#include "game/components/debug_components.h"
#include "game/components/debug_gizmo_component.h"
#include "game/components/physics/physics_body_component.h"
#include "physics/physics_system.h"
#include "platform/paths.h"

namespace Game
{
void DebugUpdate(Core::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->inputFrame->GetKey(Key::M).pressed) {
        auto musicPath = Platform::GetAssetPath() / "audio/the_entertainer.ogg";
        auto _musicPath = Core::Path(musicPath);
        Core::Handle<Audio::WillAudio> testMusic = ctx->audioManager->LoadAudio("test_music", _musicPath);
        ctx->audioManager->PlayMusic(testMusic);
    }
    if (state->inputFrame->GetKey(Key::N).pressed) {
        ctx->audioManager->SetMusicVolume(0.25f);
    }
    if (state->inputFrame->GetKey(Key::B).pressed) {
        ctx->audioManager->SetMusicVolume(1.0f);
    }
}

void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    state->registry.clear<Component::AntiGravityTag>();

    for (const auto& event : state->resolvedAddedEvents) {
        entt::entity e1 = event.e1;
        entt::entity e2 = event.e2;
        if (e1 == entt::null || e2 == entt::null) continue;

        if (state->registry.all_of<Component::FloorTag>(e2))
            state->registry.emplace_or_replace<Component::AntiGravityTag>(e1);
        else if (state->registry.all_of<Component::FloorTag>(e1))
            state->registry.emplace_or_replace<Component::AntiGravityTag>(e2);
    }


}

void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();
    auto view = state->registry.view<Component::AntiGravityTag, Component::PhysicsBodyComponent>();
    for (const auto& [entity, body] : view.each()) {
        bodyInterface.AddImpulse(body.bodyID, JPH::Vec3(0, 100.0f, 0));
    }
}

#ifndef PACKAGED_BUILD
void DebugRender(Core::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    auto& vf = frameBuffer->mainViewFamily;
    auto view = state->registry.view<Component::DebugGizmoComponent, Component::TransformComponent>();
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
} // Game
