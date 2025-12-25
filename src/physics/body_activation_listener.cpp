//
// Created by William on 2025-12-25.
//

#include "body_activation_listener.h"

#include "spdlog/spdlog.h"

namespace Physics
{
BodyActivationListener::BodyActivationListener() = default;

BodyActivationListener::~BodyActivationListener() = default;

void BodyActivationListener::OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData)
{
    size_t idx = activatedCount.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_BODY_ACTIVATION_EVENTS) {
        SPDLOG_WARN("[BodyActivationListener::OnBodyActivated] Max body activations have been reached on this frame");
        return;
    }
    activatedEvents[idx] = {inBodyID, inBodyUserData};
}

void BodyActivationListener::OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData)
{
    size_t idx = deactivatedCount.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_BODY_ACTIVATION_EVENTS) {
        SPDLOG_WARN("[BodyActivationListener::OnBodyDeactivated] Max body deactivations have been reached on this frame");
        return;
    }
    deactivatedEvents[idx] = {inBodyID, inBodyUserData};
}

std::span<const DeferredBodyActivationEvent> BodyActivationListener::GetActivatedEvents()
{
    size_t count = std::min(activatedCount.load(std::memory_order_acquire), MAX_BODY_ACTIVATION_EVENTS);
    return {activatedEvents.data(), count};
}

std::span<const DeferredBodyActivationEvent> BodyActivationListener::GetDeactivatedEvents()
{
    size_t count = std::min(deactivatedCount.load(std::memory_order_acquire), MAX_BODY_ACTIVATION_EVENTS);
    return {deactivatedEvents.data(), count};
}

void BodyActivationListener::ClearEvents()
{
    activatedCount.store(0, std::memory_order_release);
    deactivatedCount.store(0, std::memory_order_release);
}
} // Physics
