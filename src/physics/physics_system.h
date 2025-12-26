//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_MANAGER_H
#define WILL_ENGINE_PHYSICS_MANAGER_H
#include <memory>

#include <Jolt/Jolt.h>

#include "body_activation_listener.h"
#include "contact_listener.h"
#include "physics_job_system.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "layers/broad_phase_layer_interface.h"
#include "layers/object_layer_pair_filter.h"
#include "layers/object_vs_broad_phase_layer_filter.h"

namespace Core
{
struct TimeFrame;
}

namespace Physics
{
class PhysicsSystem
{
public:
    PhysicsSystem();

    PhysicsSystem(enki::TaskScheduler* scheduler);

    ~PhysicsSystem();

    void Step(float deltaTime);

    std::span<const DeferredBodyActivationEvent> GetActivatedEvents()
    {
        return bodyActivationListener.GetActivatedEvents();
    }

    std::span<const DeferredBodyActivationEvent> GetDeactivatedEvents()
    {
        return bodyActivationListener.GetDeactivatedEvents();
    }

    std::span<const DeferredCollisionEvent> GetCollisionEvents()
    {
        return contactListener.GetCollisionEvents();
    }

    void ClearEvents()
    {
        bodyActivationListener.ClearEvents();
        contactListener.ClearEvents();
    }

    JPH::BodyInterface& GetBodyInterface() { return physicsSystem.GetBodyInterface(); }
    JPH::PhysicsSystem& GetPhysicsSystem() { return physicsSystem; }

    static void RegisterAllocator()
    {
        JPH::RegisterDefaultAllocator();
    }

private:
    enki::TaskScheduler* scheduler{};

    std::unique_ptr<PhysicsJobSystem> jobSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator{};
    JPH::PhysicsSystem physicsSystem;

    BPLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;

    BodyActivationListener bodyActivationListener;
    ContactListener contactListener;
};
} // Physics

#endif //WILL_ENGINE_PHYSICS_MANAGER_H
