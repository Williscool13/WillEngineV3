//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_CONTACT_LISTENER_H
#define WILL_ENGINE_CONTACT_LISTENER_H

#include <array>
#include <span>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include "physics_config.h"


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

class ContactListener : public JPH::ContactListener
{
public:
    ContactListener();

    ~ContactListener() override;

    std::span<const DeferredCollisionEvent> GetCollisionEvents();

    void ClearEvents();

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,const JPH::ContactManifold& inManifold,JPH::ContactSettings& ioSettings) override;

    // void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;

    // void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

private:
    std::array<DeferredCollisionEvent, MAX_COLLISION_EVENTS> deferredEvents;
    std::atomic<uint32_t> eventCount{0};

    std::atomic<int32_t> warnCount{0};
};
} // Physics

#endif //WILL_ENGINE_CONTACT_LISTENER_H
