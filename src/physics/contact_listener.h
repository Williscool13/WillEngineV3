//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_CONTACT_LISTENER_H
#define WILL_ENGINE_CONTACT_LISTENER_H

#include <array>
#include <atomic>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>

#include "physics_config.h"
#include "core/containers/span.h"
#include "core/containers/array.h"


namespace Physics
{
struct DeferredCollisionEvent
{
    JPH::BodyID body1;
    JPH::BodyID body2;
    JPH::Vec3 worldNormal;
    JPH::Vec3 contactPoint;
    float penetrationDepth;
};

struct DeferredRemovedEvent
{
    JPH::BodyID body1;
    JPH::BodyID body2;
};

class ContactListener : public JPH::ContactListener
{
public:
    ContactListener();
    ~ContactListener() override;

    Core::Span<const DeferredCollisionEvent> GetAddedEvents();
    Core::Span<const DeferredCollisionEvent> GetPersistedEvents();
    Core::Span<const DeferredRemovedEvent>   GetRemovedEvents();

    void ClearEvents();

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;
    void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;
    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

private:
    void PushContactEvent(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold,
                          Core::Array<DeferredCollisionEvent, 8192>& events,
                          std::atomic<uint32_t>& count, std::atomic<int32_t>& warnCount, const char* label);

    Core::Array<DeferredCollisionEvent, MAX_COLLISION_EVENTS> addedEvents;
    std::atomic<uint32_t> addedCount{0};
    std::atomic<int32_t>  addedWarnCount{0};

    Core::Array<DeferredCollisionEvent, MAX_COLLISION_EVENTS> persistedEvents;
    std::atomic<uint32_t> persistedCount{0};
    std::atomic<int32_t>  persistedWarnCount{0};

    Core::Array<DeferredRemovedEvent, MAX_COLLISION_EVENTS> removedEvents;
    std::atomic<uint32_t> removedCount{0};
    std::atomic<int32_t>  removedWarnCount{0};
};
} // Physics

#endif //WILL_ENGINE_CONTACT_LISTENER_H
