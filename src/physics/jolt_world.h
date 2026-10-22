//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_JOLT_WORLD_H
#define WILL_ENGINE_JOLT_WORLD_H

#include <span>

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
#include "core/memory/memory_manager.h"

namespace Core
{
struct ViewFamily;
}

namespace enki
{
class TaskScheduler;
}

namespace Physics
{
/** The Jolt side of PhysicsWorld. Only the engine's physics integration includes this; everything else goes through PhysicsWorld. */
class JoltWorld
{
public:
    JoltWorld(Core::MemoryManager& memoryManager, enki::TaskScheduler* scheduler);
    ~JoltWorld();

    JoltWorld(const JoltWorld&) = delete;
    JoltWorld& operator=(const JoltWorld&) = delete;

    void Step(float deltaTime);

    std::span<const DeferredBodyActivationEvent> GetActivatedEvents() { return bodyActivationListener.GetActivatedEvents(); }
    std::span<const DeferredBodyActivationEvent> GetDeactivatedEvents() { return bodyActivationListener.GetDeactivatedEvents(); }
    std::span<const DeferredCollisionEvent> GetAddedEvents() { return contactListener.GetAddedEvents(); }
    std::span<const DeferredCollisionEvent> GetPersistedEvents() { return contactListener.GetPersistedEvents(); }
    std::span<const DeferredRemovedEvent> GetRemovedEvents() { return contactListener.GetRemovedEvents(); }

    ContactListener& GetContactListener() { return contactListener; }
    void ClearCollisionEvents() { contactListener.ClearEvents(); }
    void ClearActivationEvents() { bodyActivationListener.ClearEvents(); }

    JPH::BodyInterface& GetBodyInterface() { return physicsSystem.GetBodyInterface(); }
    const JPH::BodyInterface& GetBodyInterface() const { return physicsSystem.GetBodyInterface(); }
    JPH::PhysicsSystem& GetPhysicsSystem() { return physicsSystem; }
    const JPH::PhysicsSystem& GetPhysicsSystem() const { return physicsSystem; }
    JPH::TempAllocator& GetTempAllocator() { return tempAllocator; }

#if JPH_DEBUG_RENDERER
    DebugDrawFilter& GetDebugDrawFilter() const { return *debugDrawFilter; }
    void SetDebugDrawForceLowestLOD(bool bForce) const { debugRenderer->SetForceLowestLOD(bForce); }
#endif
    void DrawDebug(Core::ViewFamily* viewFamily, bool bUseFilter, bool bSleepColor);

private:
    void RegisterPhysics() const;
    void UnregisterPhysics() const;

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

#endif //WILL_ENGINE_JOLT_WORLD_H
