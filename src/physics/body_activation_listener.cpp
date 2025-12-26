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
        int32_t count = activationWarnCount.fetch_add(1, std::memory_order_relaxed);
        if (count == 0) {
            SPDLOG_WARN("[BodyActivationListener::OnBodyDeactivated] Max body activations reached (first occurrence)");
        } else if (count < 3) {
            SPDLOG_DEBUG("[BodyActivationListener::OnBodyDeactivated] Max body activations reached (occurrence {})", count + 1);
        }
        return;
    }
    activatedEvents[idx] = {inBodyID, inBodyUserData};
}

void BodyActivationListener::OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData)
{
    size_t idx = deactivatedCount.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_BODY_ACTIVATION_EVENTS) {
        int32_t count = deactivationWarnCount.fetch_add(1, std::memory_order_relaxed);
        if (count == 0) {
            SPDLOG_WARN("[BodyActivationListener::OnBodyDeactivated] Max body deactivations reached (first occurrence)");
        } else if (count < 3) {
            SPDLOG_DEBUG("[BodyActivationListener::OnBodyDeactivated] Max body deactivations reached (occurrence {})", count + 1);
        }
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
