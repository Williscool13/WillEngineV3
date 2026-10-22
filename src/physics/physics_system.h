//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_MANAGER_H
#define WILL_ENGINE_PHYSICS_MANAGER_H
#include <memory>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "physics_temp_allocator.h"
#include "body_activation_listener.h"
#include "contact_listener.h"
#include "physics_debug_filter.h"
#include "physics_debug_renderer.h"
#include "physics_job_system.h"
#include "layers/broad_phase_layer_interface.h"
#include "layers/object_layer_pair_filter.h"
#include "layers/object_vs_broad_phase_layer_filter.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/memory/memory_manager.h"
#include "core/types/aabb.h"

namespace Core
{
struct TimeFrame;
struct ViewFamily;
}

namespace Physics
{
class PhysicsSystem
{
public:
    PhysicsSystem();

    explicit PhysicsSystem(Core::MemoryManager& memoryManager, enki::TaskScheduler* scheduler);

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

    std::span<const DeferredCollisionEvent> GetAddedEvents()     { return contactListener.GetAddedEvents(); }
    std::span<const DeferredCollisionEvent> GetPersistedEvents() { return contactListener.GetPersistedEvents(); }
    std::span<const DeferredRemovedEvent>   GetRemovedEvents()   { return contactListener.GetRemovedEvents(); }

    ContactListener& GetContactListener() { return contactListener; }

    void ClearCollisionEvents()
    {
        contactListener.ClearEvents();
    }

    void ClearActivationEvents()
    {
        bodyActivationListener.ClearEvents();
    }

    JPH::BodyInterface& GetBodyInterface() { return physicsSystem.GetBodyInterface(); }
    JPH::PhysicsSystem& GetPhysicsSystem() { return physicsSystem; }
    JPH::TempAllocator& GetTempAllocator() { return tempAllocator; }

    Core::ArenaFixedVector<JPH::BodyID> QueryBodiesInBox(Core::Arena* arena, const AABB& box) const;
    AABB GetWorldBounds(JPH::BodyID body) const;
    AABB GetLocalBounds(JPH::BodyID body) const;
    Mat4 GetCenterOfMassTransform(JPH::BodyID body) const;

#if JPH_DEBUG_RENDERER
    DebugDrawFilter& GetDebugDrawFilter() const { return *debugDrawFilter; }
    void SetDebugDrawForceLowestLOD(bool bForce) const { debugRenderer->SetForceLowestLOD(bForce); }
#endif
    void DrawDebug(Core::ViewFamily* viewFamily, bool bUseFilter = true, bool bSleepColor = false);

    // Must be called before any Jolt allocation; safe to call from game DLL on load.
    static void RegisterAllocators(Core::MemoryManager& memoryManager);

    void RegisterPhysics() const;

    void UnregisterPhysics() const;

private:
    Core::MemoryManager* memoryManager{};
    enki::TaskScheduler* scheduler{};

    PhysicsJobSystem* jobSystem{};
    PhysicsTempAllocator tempAllocator{};
    JPH::PhysicsSystem physicsSystem;
    uint32_t reportedUpdateErrors{0};

    BPLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;

    BodyActivationListener bodyActivationListener;
    ContactListener contactListener;

#if JPH_DEBUG_RENDERER
    DebugRenderer* debugRenderer{};
    DebugDrawFilter* debugDrawFilter{};
#endif
};
} // Physics

#endif //WILL_ENGINE_PHYSICS_MANAGER_H