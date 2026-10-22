//
// Created by William on 2025-12-25.
//

#include "physics_system.h"

#include <algorithm>
#include <cstdarg>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "body_activation_listener.h"
#include "contact_listener.h"
#include "physics_helpers.h"
#include "physics_job_system.h"
#include "layers/layer_interface.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"


#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine)
{
    auto msg = Core::InlineString<512>::Format("JPH Assert Failed: %s | %s (%s:%u)", inExpression, inMessage, inFile, inLine);
    SPDLOG_ERROR("{}", msg.c_str());
    return true;
};
#endif

static void TraceImpl(const char* inFMT, ...)
{
    va_list args;
    va_start(args, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, args);
    va_end(args);

    SPDLOG_TRACE("[Jolt] {}", buffer);
}


// ---- Jolt custom allocator ----

static Core::MemoryManager* sPhysicsMemory = nullptr;

// Jolt uses JPH::Allocate/Free from multiple threads (requires thread-safe allocator).
// TempAllocatorImpl is single-threaded per-update.

// For SIMD-containing types (PhysicsSystem, MotionProperties, etc.), the custom allocator must return alignment that matches the type's alignof.
// For example, mGravity is stored in JPH::PhysicsSystem so it needs to be aligned.

static void* JoltAlloc(size_t size)
{
    return sPhysicsMemory->PhysicsAllocRaw(size, JPH_RVECTOR_ALIGNMENT);
}

static void* JoltRealloc(void* ptr, size_t, size_t newSize)
{
    return sPhysicsMemory->PhysicsRealloc(ptr, newSize, JPH_RVECTOR_ALIGNMENT);
}

static void JoltFree(void* ptr)
{
    sPhysicsMemory->PhysicsFree(ptr);
}

static void* JoltAlignedAlloc(size_t size, size_t alignment)
{
    return sPhysicsMemory->PhysicsAllocRaw(size, alignment);
}

static void JoltAlignedFree(void* ptr)
{
    sPhysicsMemory->PhysicsFree(ptr);
}

namespace Physics
{
PhysicsSystem::PhysicsSystem() = default;

void PhysicsSystem::RegisterAllocators(Core::MemoryManager& memoryManager)
{
    sPhysicsMemory = &memoryManager;
    JPH::Allocate = JoltAlloc;
    JPH::Reallocate = JoltRealloc;
    JPH::Free = JoltFree;
    JPH::AlignedAllocate = JoltAlignedAlloc;
    JPH::AlignedFree = JoltAlignedFree;
}

void PhysicsSystem::RegisterPhysics() const
{
    RegisterAllocators(*memoryManager);
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void PhysicsSystem::UnregisterPhysics() const
{
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

PhysicsSystem::PhysicsSystem(Core::MemoryManager& memoryManager, enki::TaskScheduler* scheduler)
    : memoryManager(&memoryManager)
    , scheduler(scheduler)
    , tempAllocator(memoryManager.Virtual(), PHYSICS_TEMP_ALLOCATOR_RESERVE)
{
    RegisterPhysics();

    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    void* jobMem = memoryManager.PhysicsAllocRaw(sizeof(PhysicsJobSystem), 64);
    jobSystem = new(jobMem) PhysicsJobSystem(scheduler, MAX_PHYSICS_JOBS, 8);

    physicsSystem.Init(MAX_PHYSICS_BODIES, PHYSICS_BODY_MUTEX_COUNT,
                       MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS,
                       broadPhaseLayerInterface,
                       objectVsBroadPhaseLayerFilter,
                       objectLayerPairFilter);
    physicsSystem.SetBodyActivationListener(&bodyActivationListener);
    physicsSystem.SetContactListener(&contactListener);

    SPDLOG_INFO("Physics System initialized | Bodies: {} | Mutexes: {} | Body Pairs: {} | Contacts: {} | Jobs: {} | Barriers: 8",
                MAX_PHYSICS_BODIES, PHYSICS_BODY_MUTEX_COUNT, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS, MAX_PHYSICS_JOBS);

#if JPH_DEBUG_RENDERER
    void* mem0 = memoryManager.PhysicsAllocRaw(sizeof(DebugRenderer), 64);
    debugRenderer = new(mem0) DebugRenderer();
    void* mem1 = memoryManager.PhysicsAllocRaw(sizeof(DebugDrawFilter), 64);
    debugDrawFilter = new(mem1) DebugDrawFilter(memoryManager);
#endif
}

PhysicsSystem::~PhysicsSystem()
{
#if JPH_DEBUG_RENDERER
    if (debugRenderer) {
        debugRenderer->~DebugRenderer();
        memoryManager->PhysicsFree(debugRenderer);
        debugRenderer = nullptr;
    }
    if (debugDrawFilter) {
        debugDrawFilter->~DebugDrawFilter();
        memoryManager->PhysicsFree(debugDrawFilter);
        debugDrawFilter = nullptr;
    }
#endif
    UnregisterPhysics();
};

void PhysicsSystem::Step(float deltaTime)
{
    const auto error = static_cast<uint32_t>(physicsSystem.Update(deltaTime, PHYSICS_COLLISION_STEPS, &tempAllocator, jobSystem));
    const uint32_t newErrors = error & ~reportedUpdateErrors;
    if (newErrors != 0) {
        reportedUpdateErrors |= newErrors;
        SPDLOG_WARN("[PhysicsSystem::Step] Jolt dropped work this step (first occurrence):{}{}{}; bodies may interpenetrate",
                    (newErrors & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ManifoldCacheFull)) ? " manifold cache full" : "",
                    (newErrors & static_cast<uint32_t>(JPH::EPhysicsUpdateError::BodyPairCacheFull)) ? " body pair cache full" : "",
                    (newErrors & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ContactConstraintsFull)) ? " contact constraints full" : "");
    }
}

Core::ArenaFixedVector<JPH::BodyID> PhysicsSystem::QueryBodiesInBox(Core::Arena* arena, const AABB& box) const
{
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
    physicsSystem.GetBroadPhaseQuery().CollideAABox(PhysicsHelpers::ToJolt(box), collector);
    auto bodies = Core::ArenaFixedVector<JPH::BodyID>(arena, std::max(collector.mHits.size(), size_t{1}));
    for (const JPH::BodyID& id : collector.mHits) { bodies.PushBack(id); }
    return bodies;
}

AABB PhysicsSystem::GetWorldBounds(JPH::BodyID body) const
{
    return PhysicsHelpers::ToGLM(physicsSystem.GetBodyInterface().GetTransformedShape(body).GetWorldSpaceBounds());
}

AABB PhysicsSystem::GetLocalBounds(JPH::BodyID body) const
{
    const JPH::RefConst<JPH::Shape> shape = physicsSystem.GetBodyInterface().GetShape(body);
    return shape ? PhysicsHelpers::ToGLM(shape->GetLocalBounds()) : AABB{};
}

Mat4 PhysicsSystem::GetCenterOfMassTransform(JPH::BodyID body) const
{
    return PhysicsHelpers::ToGLM(physicsSystem.GetBodyInterface().GetCenterOfMassTransform(body));
}

uint64_t PhysicsSystem::GetUserData(JPH::BodyID body) const
{
    return physicsSystem.GetBodyInterface().GetUserData(body);
}

Vec3 PhysicsSystem::GetPosition(JPH::BodyID body) const
{
    return PhysicsHelpers::ToGLM(physicsSystem.GetBodyInterface().GetPosition(body));
}

Vec3 PhysicsSystem::GetCenterOfMassPosition(JPH::BodyID body) const
{
    return PhysicsHelpers::ToGLM(physicsSystem.GetBodyInterface().GetCenterOfMassPosition(body));
}

Vec3 PhysicsSystem::GetLinearVelocity(JPH::BodyID body) const
{
    return PhysicsHelpers::ToGLM(physicsSystem.GetBodyInterface().GetLinearVelocity(body));
}

Vec3 PhysicsSystem::GetAngularVelocity(JPH::BodyID body) const
{
    return PhysicsHelpers::ToGLM(physicsSystem.GetBodyInterface().GetAngularVelocity(body));
}

BodyState PhysicsSystem::GetBodyState(JPH::BodyID body) const
{
    const JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
    const JPH::EMotionType motionType = bodyInterface.GetMotionType(body);
    return {
        .motionType = motionType == JPH::EMotionType::Static ? MotionType::Static : motionType == JPH::EMotionType::Kinematic ? MotionType::Kinematic : MotionType::Dynamic,
        .objectLayer = bodyInterface.GetObjectLayer(body),
        .bAwake = bodyInterface.IsActive(body),
        .linearVelocity = PhysicsHelpers::ToGLM(bodyInterface.GetLinearVelocity(body)),
        .angularVelocity = PhysicsHelpers::ToGLM(bodyInterface.GetAngularVelocity(body)),
    };
}

bool PhysicsSystem::IsAwake(JPH::BodyID body) const
{
    return physicsSystem.GetBodyInterface().IsActive(body);
}

void PhysicsSystem::AddImpulse(JPH::BodyID body, const Vec3& impulse)
{
    physicsSystem.GetBodyInterface().AddImpulse(body, PhysicsHelpers::ToJolt(impulse));
}

void PhysicsSystem::AddTorque(JPH::BodyID body, const Vec3& torque)
{
    physicsSystem.GetBodyInterface().AddTorque(body, PhysicsHelpers::ToJolt(torque));
}

class IgnoredLayersFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit IgnoredLayersFilter(uint32_t ignored) : ignored(ignored) {}

    bool ShouldCollide(JPH::ObjectLayer layer) const override { return (ignored & Layers::Bit(layer)) == 0; }

private:
    uint32_t ignored;
};

bool PhysicsSystem::CastRay(const Vec3& origin, const Vec3& direction, CastHit& hit, JPH::BodyID ignore, uint32_t ignoredLayers) const
{
    const JPH::RRayCast ray{PhysicsHelpers::ToJoltR(origin), PhysicsHelpers::ToJolt(direction)};
    JPH::RayCastResult result;
    const IgnoredLayersFilter layerFilter(ignoredLayers);
    const JPH::IgnoreSingleBodyFilter bodyFilter(ignore);
    if (!physicsSystem.GetNarrowPhaseQuery().CastRay(ray, result, {}, layerFilter, bodyFilter)) { return false; }
    hit = {result.mFraction, result.mBodyID};
    return true;
}

bool PhysicsSystem::CastSphere(float radius, const Vec3& origin, const Vec3& direction, CastHit& hit, JPH::BodyID ignore, uint32_t ignoredLayers) const
{
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const JPH::RShapeCast cast(&sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(PhysicsHelpers::ToJoltR(origin)), PhysicsHelpers::ToJolt(direction));
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const IgnoredLayersFilter layerFilter(ignoredLayers);
    const JPH::IgnoreSingleBodyFilter bodyFilter(ignore);
    physicsSystem.GetNarrowPhaseQuery().CastShape(cast, {}, JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);
    if (!collector.HadHit()) { return false; }
    hit = {collector.mHit.mFraction, collector.mHit.mBodyID2};
    return true;
}

Core::ArenaFixedVector<BodyTouch> PhysicsSystem::QueryTouching(Core::Arena* arena, JPH::BodyID body, float maxSeparation, size_t capacity) const
{
    constexpr size_t MAX_SUB_SHAPES = 16;
    struct Touch
    {
        BodyTouch touch;
        Core::InlineVector<JPH::SubShapeID, MAX_SUB_SHAPES> subShapes{};
    };

    const JPH::TransformedShape shape = physicsSystem.GetBodyInterface().GetTransformedShape(body);
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = maxSeparation;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const JPH::IgnoreSingleBodyFilter ignoreSelf(body);
    physicsSystem.GetNarrowPhaseQuery().CollideShape(shape.mShape, shape.GetShapeScale(), shape.GetCenterOfMassTransform(), settings, shape.GetCenterOfMassTransform().GetTranslation(),
                                                     collector, {}, {}, ignoreSelf);

    auto touches = Core::ArenaFixedVector<Touch>(arena, std::max(capacity, size_t{1}));
    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        Touch* touch = nullptr;
        for (Touch& t : touches) {
            if (t.touch.body == hit.mBodyID2) { touch = &t; }
        }
        if (!touch) {
            if (touches.Size() >= capacity) { continue; }
            touches.PushBack({.touch = {.body = hit.mBodyID2}});
            touch = &touches[touches.Size() - 1];
        }
        if (!touch->subShapes.Contains(hit.mSubShapeID2) && !touch->subShapes.IsFull()) { touch->subShapes.PushBack(hit.mSubShapeID2); }
        ++touch->touch.contacts;
        touch->touch.maxPenetration = std::max(touch->touch.maxPenetration, hit.mPenetrationDepth);
    }

    auto out = Core::ArenaFixedVector<BodyTouch>(arena, std::max(touches.Size(), size_t{1}));
    for (Touch& t : touches) {
        t.touch.subShapes = static_cast<uint32_t>(t.subShapes.Size());
        out.PushBack(t.touch);
    }
    return out;
}

void PhysicsSystem::DrawDebug(Core::ViewFamily* viewFamily, bool bUseFilter, bool bSleepColor)
{
#if JPH_DEBUG_RENDERER
    debugRenderer->SetViewFamily(viewFamily);

    JPH::BodyManager::DrawSettings settings;
    settings.mDrawShape = true;
    settings.mDrawShapeWireframe = true;
    settings.mDrawShapeColor = bSleepColor ? JPH::BodyManager::EShapeColor::SleepColor : JPH::BodyManager::EShapeColor::InstanceColor;

    if (bUseFilter) {
        physicsSystem.DrawBodies(settings, debugRenderer, debugDrawFilter);
    } else {
        physicsSystem.DrawBodies(settings, debugRenderer, nullptr);
    }
#endif
}

} // Physics
