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
enum class MotionType : uint8_t { Static, Kinematic, Dynamic };

struct CastHit
{
    /** Along the cast, 0 at its origin and 1 at its end. */
    float fraction{1.0f};
    JPH::BodyID body;
};

struct BodyState
{
    MotionType motionType{MotionType::Static};
    uint16_t objectLayer{0};
    bool bAwake{false};
    Vec3 linearVelocity{0.0f};
    Vec3 angularVelocity{0.0f};
};

/** Another body a body's shape is touching: contact count, distinct sub-shapes touched, and the deepest penetration. */
struct BodyTouch
{
    JPH::BodyID body;
    uint32_t contacts{0};
    uint32_t subShapes{0};
    float maxPenetration{-1e30f};
};

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
    /** 0 once the body is gone; see BodyUserData. */
    uint64_t GetUserData(JPH::BodyID body) const;

    Vec3 GetPosition(JPH::BodyID body) const;
    Vec3 GetCenterOfMassPosition(JPH::BodyID body) const;
    Vec3 GetLinearVelocity(JPH::BodyID body) const;
    Vec3 GetAngularVelocity(JPH::BodyID body) const;
    BodyState GetBodyState(JPH::BodyID body) const;
    bool IsAwake(JPH::BodyID body) const;
    void AddImpulse(JPH::BodyID body, const Vec3& impulse);
    void AddTorque(JPH::BodyID body, const Vec3& torque);

    /** Closest hit along origin + t * direction for t in [0, 1], skipping `ignore` and the layers in `ignoredLayers` (Layers::Bit). */
    bool CastRay(const Vec3& origin, const Vec3& direction, CastHit& hit, JPH::BodyID ignore = {}, uint32_t ignoredLayers = 0) const;
    /** As CastRay, sweeping a sphere. */
    bool CastSphere(float radius, const Vec3& origin, const Vec3& direction, CastHit& hit, JPH::BodyID ignore = {}, uint32_t ignoredLayers = 0) const;
    /** Bodies within maxSeparation of this body's shape, at most `capacity` of them. */
    Core::ArenaFixedVector<BodyTouch> QueryTouching(Core::Arena* arena, JPH::BodyID body, float maxSeparation, size_t capacity) const;

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