//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_WORLD_H
#define WILL_ENGINE_PHYSICS_WORLD_H

#include <cstdint>

#include "body_handle.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/types/aabb.h"

namespace Core
{
struct TimeFrame;
struct ViewFamily;
class MemoryManager;
}

namespace enki
{
class TaskScheduler;
}

namespace Physics
{
class JoltWorld;

enum class MotionType : uint8_t { Static, Kinematic, Dynamic };

struct BodyCounts
{
    uint32_t total{0};
    uint32_t staticBodies{0};
    uint32_t kinematic{0};
    uint32_t kinematicAwake{0};
    uint32_t dynamic{0};
    uint32_t dynamicAwake{0};
};

struct CastHit
{
    /** Along the cast, 0 at its origin and 1 at its end. */
    float fraction{1.0f};
    BodyHandle body;
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
    BodyHandle body;
    uint32_t contacts{0};
    uint32_t subShapes{0};
    float maxPenetration{-1e30f};
};

class PhysicsWorld
{
public:
    static constexpr const char* COMPONENT_NAME = "PhysicsWorld";

    explicit PhysicsWorld(Core::MemoryManager& memoryManager, enki::TaskScheduler* scheduler);
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    /** The Jolt side, for the engine's physics integration only (jolt_world.h). */
    JoltWorld& Jolt() { return *jolt; }
    const JoltWorld& Jolt() const { return *jolt; }

    void Step(float deltaTime);
    void ClearCollisionEvents();
    void ClearActivationEvents();

    Core::ArenaFixedVector<BodyHandle> QueryBodiesInBox(Core::Arena* arena, const AABB& box) const;
    AABB GetWorldBounds(BodyHandle body) const;
    /** The body's shape bounds in its centre-of-mass frame, which GetCenterOfMassTransform maps to world. */
    AABB GetLocalBounds(BodyHandle body) const;
    Mat4 GetCenterOfMassTransform(BodyHandle body) const;
    /** 0 once the body is gone; see BodyUserData. */
    uint64_t GetUserData(BodyHandle body) const;

    Vec3 GetPosition(BodyHandle body) const;
    Vec3 GetCenterOfMassPosition(BodyHandle body) const;
    Vec3 GetLinearVelocity(BodyHandle body) const;
    Vec3 GetAngularVelocity(BodyHandle body) const;
    BodyState GetBodyState(BodyHandle body) const;
    bool IsAwake(BodyHandle body) const;
    uint32_t GetAwakeBodyCount() const;
    BodyCounts GetBodyCounts() const;
    void AddImpulse(BodyHandle body, const Vec3& impulse);
    void AddTorque(BodyHandle body, const Vec3& torque);
    /** Turns a kinematic or dynamic body dynamic in place, awake and moving at linearVelocity. */
    void MakeDynamic(BodyHandle body, const Vec3& linearVelocity);

    /** Closest hit along origin + t * direction for t in [0, 1], skipping `ignore` and the layers in `ignoredLayers` (Layers::Bit). */
    bool CastRay(const Vec3& origin, const Vec3& direction, CastHit& hit, BodyHandle ignore = {}, uint32_t ignoredLayers = 0) const;
    /** As CastRay, sweeping a sphere. */
    bool CastSphere(float radius, const Vec3& origin, const Vec3& direction, CastHit& hit, BodyHandle ignore = {}, uint32_t ignoredLayers = 0) const;
    /** Bodies within maxSeparation of this body's shape, at most `capacity` of them. */
    Core::ArenaFixedVector<BodyTouch> QueryTouching(Core::Arena* arena, BodyHandle body, float maxSeparation, size_t capacity) const;

    void DrawDebug(Core::ViewFamily* viewFamily, bool bUseFilter = true, bool bSleepColor = false);

    // Must be called before any Jolt allocation; safe to call from game DLL on load.
    static void RegisterAllocators(Core::MemoryManager& memoryManager);

private:
    Core::MemoryManager* memoryManager{};
    JoltWorld* jolt{};
};
} // Physics

#endif //WILL_ENGINE_PHYSICS_WORLD_H
