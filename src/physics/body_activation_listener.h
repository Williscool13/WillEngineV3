//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_BODY_ACTIVATION_LISTENER_H
#define WILL_ENGINE_BODY_ACTIVATION_LISTENER_H

#include <array>
#include <span>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "physics_config.h"

namespace Physics
{
struct DeferredBodyActivationEvent
{
    JPH::BodyID bodyId;
    uint64_t bodyUserData{};
};

class BodyActivationListener : public JPH::BodyActivationListener
{
public:
    BodyActivationListener();

    ~BodyActivationListener() override;

    std::span<const DeferredBodyActivationEvent> GetActivatedEvents();

    std::span<const DeferredBodyActivationEvent> GetDeactivatedEvents();

    void ClearEvents();

private:
    void OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override;

    void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override;

private:
    std::array<DeferredBodyActivationEvent, MAX_BODY_ACTIVATION_EVENTS> activatedEvents;
    std::array<DeferredBodyActivationEvent, MAX_BODY_ACTIVATION_EVENTS> deactivatedEvents;
    std::atomic<uint32_t> activatedCount{0};
    std::atomic<uint32_t> deactivatedCount{0};

    std::atomic<int32_t> deactivationWarnCount{0};
    std::atomic<int32_t> activationWarnCount{0};

};
} // Physics

#endif //WILL_ENGINE_BODY_ACTIVATION_LISTENER_H
