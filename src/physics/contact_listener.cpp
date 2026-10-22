//
// Created by William on 2025-12-25.
//

#include "contact_listener.h"

#include "body_user_data.h"
#include "spdlog/spdlog.h"

namespace Physics
{
ContactListener::ContactListener() = default;

ContactListener::~ContactListener() = default;

Core::Span<const DeferredCollisionEvent> ContactListener::GetAddedEvents()
{
    size_t count = std::min(addedCount.load(std::memory_order_acquire), MAX_COLLISION_EVENTS);
    return {addedEvents.Data(), count};
}

Core::Span<const DeferredCollisionEvent> ContactListener::GetPersistedEvents()
{
    size_t count = std::min(persistedCount.load(std::memory_order_acquire), MAX_COLLISION_EVENTS);
    return {persistedEvents.Data(), count};
}

Core::Span<const DeferredRemovedEvent> ContactListener::GetRemovedEvents()
{
    size_t count = std::min(removedCount.load(std::memory_order_acquire), MAX_COLLISION_EVENTS);
    return {removedEvents.Data(), count};
}

void ContactListener::ClearEvents()
{
    addedCount.store(0, std::memory_order_release);
    persistedCount.store(0, std::memory_order_release);
    removedCount.store(0, std::memory_order_release);
}

void ContactListener::PushContactEvent(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold,
                                       Core::Array<DeferredCollisionEvent, MAX_COLLISION_EVENTS>& events,
                                       std::atomic<uint32_t>& count, std::atomic<int32_t>& warnCount, const char* label)
{
    size_t idx = count.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_COLLISION_EVENTS) {
        int32_t w = warnCount.fetch_add(1, std::memory_order_relaxed);
        if (w == 0) {
            SPDLOG_WARN("[ContactListener] {} max events reached (first occurrence)", label);
        } else if (w < 3) {
            SPDLOG_DEBUG("[ContactListener] {} max events reached (occurrence {})", label, w + 1);
        }
        return;
    }

    events[idx] = {
        inBody1.GetID(),
        inBody2.GetID(),
        inBody1.GetUserData(),
        inBody2.GetUserData(),
        inManifold.mWorldSpaceNormal,
        inManifold.GetWorldSpaceContactPointOn1(0),
        inManifold.mPenetrationDepth
    };
}

bool ContactListener::SetConveyor(uint32_t slot, const ConveyorSurface& surface)
{
    ConveyorSurface& current = conveyors[slot];
    const bool bChanged = slot >= conveyorCount || current.body != surface.body || current.linearVelocity != surface.linearVelocity ||
                          current.angularVelocity != surface.angularVelocity || current.centre != surface.centre;
    current = surface;
    return bChanged;
}

const ConveyorSurface* ContactListener::FindConveyor(const JPH::Body& body) const
{
    const uint32_t slot = BodyUserData::ConveyorSlot(body.GetUserData());
    if (slot == 0 || slot > conveyorCount) { return nullptr; }
    const ConveyorSurface& surface = conveyors[slot - 1];
    return surface.body == body.GetID() ? &surface : nullptr;
}

void ContactListener::ApplyConveyors(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::ContactSettings& ioSettings) const
{
    if (conveyorCount == 0) { return; }
    const ConveyorSurface* surface1 = FindConveyor(inBody1);
    const ConveyorSurface* surface2 = FindConveyor(inBody2);
    if (!surface1 && !surface2) { return; }

    const JPH::RVec3 com1 = inBody1.GetCenterOfMassPosition();
    auto add = [&](const ConveyorSurface& surface, float sign) {
        ioSettings.mRelativeAngularSurfaceVelocity += sign * surface.angularVelocity;
        ioSettings.mRelativeLinearSurfaceVelocity += sign * (surface.linearVelocity + surface.angularVelocity.Cross(JPH::Vec3(com1 - surface.centre)));
    };
    if (surface1) { add(*surface1, -1.0f); }
    if (surface2) { add(*surface2, 1.0f); }
}

void ContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
{
    ApplyConveyors(inBody1, inBody2, ioSettings);
    PushContactEvent(inBody1, inBody2, inManifold, addedEvents, addedCount, addedWarnCount, "Added");
}

void ContactListener::OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
{
    ApplyConveyors(inBody1, inBody2, ioSettings);
    PushContactEvent(inBody1, inBody2, inManifold, persistedEvents, persistedCount, persistedWarnCount, "Persisted");
}

void ContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
{
    size_t idx = removedCount.fetch_add(1, std::memory_order_relaxed);
    if (idx >= MAX_COLLISION_EVENTS) {
        int32_t w = removedWarnCount.fetch_add(1, std::memory_order_relaxed);
        if (w == 0) {
            SPDLOG_WARN("[ContactListener] Removed max events reached (first occurrence)");
        } else if (w < 3) {
            SPDLOG_DEBUG("[ContactListener] Removed max events reached (occurrence {})", w + 1);
        }
        return;
    }

    removedEvents[idx] = {
        inSubShapePair.GetBody1ID(),
        inSubShapePair.GetBody2ID()
    };
}
} // Physics
