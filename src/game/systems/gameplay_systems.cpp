//
// Created by William on 2026-03-26.
//

#include "gameplay_systems.h"

#include <algorithm>
#include <cmath>

#include <tracy/Tracy.hpp>

#include "core/time/time_frame.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"
#include "game/components/gameplay/path_mover_component.h"

namespace Game
{
void UpdatePathMovers(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    const float dt = state->timeFrame->deltaTime;

    auto view = state->registry.view<Component::PathMoverComponent, Component::TransformComponent>();
    for (auto entity : view) {
        auto& mover = view.get<Component::PathMoverComponent>(entity);
        auto& transform = view.get<Component::TransformComponent>(entity);

        if (mover.controlPoints.Size() < 2) {
            continue;
        }

        glm::vec3 oldPos;
        glm::quat oldRot;
        Component::EvaluatePath(mover.controlPoints, mover.progress, mover.direction, oldPos, oldRot);

        auto totalPathLength = static_cast<float>(mover.controlPoints.Size() - 1);
        float progressDelta = (mover.speed * dt) / totalPathLength;
        mover.progress += progressDelta * static_cast<float>(mover.direction);

        switch (mover.loopMode) {
        case Component::PathLoopMode::Once:
            mover.progress = std::clamp(mover.progress, 0.0f, 1.0f);
            break;
        case Component::PathLoopMode::Loop:
            if (mover.progress >= 1.0f) {
                mover.progress -= 1.0f;
            } else if (mover.progress < 0.0f) {
                mover.progress += 1.0f;
            }
            break;
        case Component::PathLoopMode::PingPong:
            if (mover.progress >= 1.0f) {
                mover.progress = 2.0f - mover.progress;
                mover.direction = -1;
            } else if (mover.progress <= 0.0f) {
                mover.progress = -mover.progress;
                mover.direction = 1;
            }
            break;
        default:
            break;
        }

        mover.progress = std::clamp(mover.progress, 0.0f, 1.0f);

        glm::vec3 newPos;
        glm::quat newRot;
        Component::EvaluatePath(mover.controlPoints, mover.progress, mover.direction, newPos, newRot);

        transform.translation += (newPos - oldPos);
        transform.rotation = glm::inverse(oldRot) * newRot * transform.rotation;
        state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    }
}
} // Game
