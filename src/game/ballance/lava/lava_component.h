//
// Created by William on 2026-10-24.
//

#ifndef WILL_ENGINE_BALLANCE_LAVA_COMPONENT_H
#define WILL_ENGINE_BALLANCE_LAVA_COMPONENT_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/component_registry.h"
#include "engine/reflection/reflection.h"
#include "game/ballance/fwd_components.h"

namespace Ballance::Component
{
/**
 * On a body that eats what lands on it. Every dynamic body (other than the player) that touches it loses its physics body and sinks at
 * `sinkSpeed` until its top is `extraDepth` below the lava's top (taking `minDuration` to `maxDuration`), tilting up to `tiltDegrees`, then is destroyed.
 */
struct LavaComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "LavaComponent";

    float sinkSpeed{0.3f};
    float extraDepth{0.3f};
    float minDuration{2.0f};
    float maxDuration{4.0f};
    float tiltDegrees{20.0f};

    WILL_REFLECT(LavaComponent,
        WILL_FIELD(sinkSpeed, .min = 0.01f, .speed = 0.01f),
        WILL_FIELD(extraDepth, .min = 0.0f, .speed = 0.01f),
        WILL_FIELD(minDuration, .min = 0.05f, .speed = 0.05f),
        WILL_FIELD(maxDuration, .min = 0.05f, .speed = 0.05f),
        WILL_FIELD(tiltDegrees, .min = 0.0f, .speed = 0.5f))
};

/** Runtime state of a body being eaten; the start pose is local to its parent. */
struct DissolvingComponent
{
    glm::vec3 startTranslation{0.0f};
    glm::quat startRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 startScale{1.0f};
    /** World down expressed in the parent's space, already scaled by the sink depth. */
    glm::vec3 sink{0.0f};
    glm::vec3 tiltAxis{1.0f, 0.0f, 0.0f};
    glm::vec3 shrinkPivot{0.0f};
    float tiltRadians{0.0f};
    float duration{1.5f};
    float elapsed{0.0f};
};

/** Kept when its dissolve finishes: the entity gets a DissolvedTag instead of being destroyed, and its owner restores it. */
struct DissolveKeepTag
{};

struct DissolvedTag
{};
}

#endif //WILL_ENGINE_BALLANCE_LAVA_COMPONENT_H
