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

        if (mover.spline.points.Size() < 2) {
            continue;
        }

        if (mover.waitTimer > 0.0f) {
            mover.waitTimer -= dt;
            state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
            continue;
        }

        const auto segmentCount = mover.spline.SegmentCount();
        const int32_t segmentMax = segmentCount - 1;
        const auto pointCount = static_cast<int32_t>(mover.spline.points.Size());

        // Finished the wait
        if (mover.bIsWaiting) {
            switch (mover.loopMode) {
                case Component::PathLoopMode::Loop:
                    if (mover.currentSegment >= segmentMax + 1) {
                        mover.currentSegment = 0;
                    }
                    break;
                case Component::PathLoopMode::PingPong:
                    if (mover.currentSegment >= pointCount - 1) {
                        mover.direction = -1;
                    }
                    else if (mover.currentSegment <= 0) {
                        mover.direction = 1;
                    }
                    break;
                case Component::PathLoopMode::Once:
                default:
                    break;
            }
            mover.bIsWaiting = false;
        }

        int32_t targetSegment = mover.currentSegment + mover.direction;
        if (targetSegment >= pointCount) {
            targetSegment = 0;
        }
        else if (targetSegment < 0) {
            targetSegment = pointCount - 1;
        }

        glm::vec3 oldPos;
        glm::quat oldRot;
        Component::EvaluatePath(mover.spline, mover.pointSettings, mover.currentSegment, targetSegment, mover.progress, oldPos, oldRot);

        const float speed = (static_cast<size_t>(targetSegment) < mover.pointSettings.Size()) ? mover.pointSettings[targetSegment].speed : 1.0f;
        const float wait = (static_cast<size_t>(targetSegment) < mover.pointSettings.Size()) ? mover.pointSettings[targetSegment].waitTime : 0.0f;

        mover.progress += speed * dt;

        if (mover.progress > 1.0f) {
            mover.bIsWaiting = true;
            mover.waitTimer = wait;
            mover.currentSegment = targetSegment;
            mover.progress = 0.0f;
        }

        glm::vec3 newPos;
        glm::quat newRot;
        Component::EvaluatePath(mover.spline, mover.pointSettings, mover.currentSegment, targetSegment, mover.progress, newPos, newRot);

        transform.translation += (newPos - oldPos);
        transform.rotation = glm::inverse(oldRot) * newRot * transform.rotation;
        state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    }
}
} // Game
