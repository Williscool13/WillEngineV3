//
// Created by William on 2026-10-17.
//

#include "motion_systems.h"

#include <tracy/Tracy.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/constants.h"
#include "core/time/time_frame.h"
#include "engine/engine_api.h"
#include "engine/components/core_components.h"
#include "engine/components/fwd_components.h"
#include "engine/components/motion/path_mover_component.h"
#include "engine/components/motion/rotate_in_place_component.h"

namespace Engine
{
void UpdatePathMovers(EngineContext* ctx, EngineState* state)
{
    ZoneScoped;
    const float dt = state->timeFrame->deltaTime;

    for (const auto& [entity, mover, transform] : state->registry.View<Component::PathMoverComponent, Engine::Mut<Component::TransformComponent>>().each()) {
        if (mover.spline.points.Size() < 2) {
            continue;
        }
        auto* run = state->registry.TryGetMut<Component::PathMoverRuntime>(entity);
        if (!run) {
            if (!mover.bAutoStart) { continue; }
            run = &state->registry.Add<Component::PathMoverRuntime>(entity, Component::PathMoverRuntime::AtStart(mover));
        }

        if (run->waitTimer > 0.0f) {
            run->waitTimer -= dt;
            state->registry.Set<Component::DirtyTransformRequest>(entity);
            continue;
        }

        const auto segmentCount = mover.spline.SegmentCount();
        const int32_t segmentMax = segmentCount - 1;
        const auto pointCount = static_cast<int32_t>(mover.spline.points.Size());

        // Finished the wait
        if (run->bIsWaiting) {
            switch (mover.loopMode) {
                case Component::PathLoopMode::Loop:
                    if (run->currentSegment >= segmentMax + 1) {
                        run->currentSegment = 0;
                    }
                    break;
                case Component::PathLoopMode::PingPong:
                    if (run->currentSegment >= pointCount - 1) {
                        run->direction = -1;
                    }
                    else if (run->currentSegment <= 0) {
                        run->direction = 1;
                    }
                    break;
                case Component::PathLoopMode::Once:
                default:
                    break;
            }
            run->bIsWaiting = false;
        }

        int32_t targetSegment = run->currentSegment + run->direction;
        if (targetSegment >= pointCount) {
            targetSegment = 0;
        }
        else if (targetSegment < 0) {
            targetSegment = pointCount - 1;
        }

        glm::vec3 oldPos;
        glm::quat oldRot;
        Component::EvaluatePath(mover.spline, mover.pointSettings, run->currentSegment, targetSegment, run->progress, oldPos, oldRot);

        const float speed = (static_cast<size_t>(targetSegment) < mover.pointSettings.Size()) ? mover.pointSettings[targetSegment].speed : 1.0f;
        const float wait = (static_cast<size_t>(targetSegment) < mover.pointSettings.Size()) ? mover.pointSettings[targetSegment].waitTime : 0.0f;

        run->progress += speed * (dt - run->waitTimer);
        run->waitTimer = 0.0f;

        if (run->progress > 1.0f) {
            run->bIsWaiting = true;
            run->waitTimer = wait - (run->progress - 1.0f) / speed;
            run->currentSegment = targetSegment;
            run->progress = 0.0f;
        }

        glm::vec3 newPos;
        glm::quat newRot;
        Component::EvaluatePath(mover.spline, mover.pointSettings, run->currentSegment, targetSegment, run->progress, newPos, newRot);

        const glm::quat anchorRot = transform.rotation * glm::inverse(oldRot);
        transform.translation += anchorRot * (newPos - oldPos);
        transform.rotation = anchorRot * newRot;
        state->registry.Set<Component::DirtyTransformRequest>(entity);
    }
}

void UpdateRotateInPlace(EngineContext* ctx, EngineState* state)
{
    ZoneScoped;
    const float dt = state->timeFrame->deltaTime;

    for (const auto& [entity, rotator, transform] : state->registry.View<Component::RotateInPlaceComponent, Engine::Mut<Component::TransformComponent>>().each()) {
        const float axisLen = glm::length(rotator.axis);
        if (axisLen < DIVISION_EPSILON || rotator.speedDegrees == 0.0f) {
            continue;
        }

        const glm::vec3 axis = rotator.axis / axisLen;
        const glm::quat delta = glm::angleAxis(glm::radians(rotator.speedDegrees * dt), axis);

        if (rotator.bWorldSpace) {
            transform.rotation = glm::normalize(delta * transform.rotation);
        }
        else {
            transform.rotation = glm::normalize(transform.rotation * delta);
        }

        state->registry.Set<Component::DirtyTransformRequest>(entity);
    }
}
} // Engine
