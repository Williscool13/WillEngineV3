//
// Created by William on 2026-10-24.
//

#ifndef WILL_ENGINE_BALLANCE_CANE_LIFT_COMPONENT_H
#define WILL_ENGINE_BALLANCE_CANE_LIFT_COMPONENT_H

#include <cstdint>

#include <glm/glm.hpp>

#include "core/types/transform.h"
#include "engine/component_registry.h"
#include "engine/components/motion/path_mover_component.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/core/entity_ref.h"
#include "engine/reflection/reflection.h"
#include "game/ballance/fwd_components.h"

namespace Ballance::Component
{
/**
 * On a cane lift's root. Every `interval` seconds an idle cart of this lift is clipped on at the boot and runs its authored path (its hooks
 * loop on their own movers in step with it). At the end of the path the cart is released: it turns dynamic, keeps the path's velocity plus
 * `releaseBoost` along it, and falls. A released cart the lava has not eaten within `releaseTimeout` seconds, or that drops below
 * `killHeight`, dissolves where it is. Every eaten cart returns to the boot and waits to be clipped on again.
 */
struct CaneLiftComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CaneLiftComponent";

    float interval{2.5f};
    /** Seconds until the first clip-on; authored to put it in step with a hook at the boot. */
    float startDelay{0.0f};
    float releaseBoost{0.3f};
    float releaseTimeout{6.0f};
    float killHeight{-40.0f};

    WILL_REFLECT(CaneLiftComponent,
        WILL_FIELD(interval, .min = 0.1f, .speed = 0.01f),
        WILL_FIELD(startDelay, .min = 0.0f, .speed = 0.01f),
        WILL_FIELD(releaseBoost, .speed = 0.01f),
        WILL_FIELD(releaseTimeout, .min = 0.1f, .speed = 0.1f),
        WILL_FIELD(killHeight, .speed = 0.1f))
};

struct CaneLiftRuntime
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CaneLiftRuntime";

    /** Seconds until the next clip-on. */
    float timer{0.0f};

    WILL_REFLECT(CaneLiftRuntime, WILL_FIELD(timer))
};

/** A pooled cart of `lift`. Authored at the boot with the kinematic body it lifts with and the PathMover (autoStart off) it runs once per trip. */
struct CaneCartComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CaneCartComponent";

    Engine::EntityRef lift;

    WILL_REFLECT(CaneCartComponent, WILL_FIELD(lift))
};

enum class CaneCartPhase : uint8_t
{
    Idle,
    Lifting,
    Falling,
    Eaten,
};

/** The park pose is the authored one, taken the first time the lift sees the cart, so every trip starts from it. */
struct CaneCartRuntime
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CaneCartRuntime";

    glm::vec3 parkTranslation{0.0f};
    glm::quat parkRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 parkScale{1.0f};
    CaneCartPhase phase{CaneCartPhase::Idle};
    glm::vec3 releaseVelocity{0.0f};
    bool bVelocityApplied{false};
    float fallTime{0.0f};

    WILL_REFLECT(CaneCartRuntime,
        WILL_FIELD(parkTranslation),
        WILL_FIELD(parkRotation),
        WILL_FIELD(parkScale),
        WILL_FIELD(phase),
        WILL_FIELD(releaseVelocity),
        WILL_FIELD(bVelocityApplied),
        WILL_FIELD(fallTime))
};
}

#endif //WILL_ENGINE_BALLANCE_CANE_LIFT_COMPONENT_H
