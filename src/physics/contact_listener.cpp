//
// Created by William on 2025-12-25.
//

#include "contact_listener.h"

#include "spdlog/spdlog.h"

namespace Physics
{
ContactListener::ContactListener() = default;

ContactListener::~ContactListener() = default;

std::span<const DeferredCollisionEvent> ContactListener::GetCollisionEvents()
{
    size_t count = std::min(eventCount.load(std::memory_order_acquire), MAX_COLLISION_EVENTS);
    return {deferredEvents.data(), count};
}

void ContactListener::ClearEvents()
{
    eventCount.store(0, std::memory_order_release);
}

void ContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
{
    size_t idx = eventCount.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_COLLISION_EVENTS) {
        int32_t count = warnCount.fetch_add(1, std::memory_order_relaxed);
        if (count == 0) {
            SPDLOG_WARN("[ContactListener::OnContactAdded] Max contact events reached (first occurrence)");
        } else if (count < 3) {
            SPDLOG_DEBUG("[ContactListener::OnContactAdded] Max contact events reached (occurrence {})", count + 1);
        }
        return;
    }

    deferredEvents[idx] = {
        inBody1.GetID(),
        inBody2.GetID(),
        inManifold.mWorldSpaceNormal,
        inManifold.GetWorldSpaceContactPointOn1(0),
        inManifold.mPenetrationDepth
    };
}
} // Physics
