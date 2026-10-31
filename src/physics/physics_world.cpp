//
// Created by William on 2025-12-25.
//

#include "physics_world.h"

#include <algorithm>
#include <cstdarg>
#include <Jolt/Jolt.h>
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

#include "jolt_world.h"
#include "physics_helpers.h"
#include "physics_layers.h"
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
using PhysicsHelpers::ToGLM;
using PhysicsHelpers::ToJolt;
using PhysicsHelpers::ToJoltR;
using PhysicsHelpers::ToHandle;

void PhysicsWorld::RegisterAllocators(Core::MemoryManager& memoryManager)
{
    sPhysicsMemory = &memoryManager;
    JPH::Allocate = JoltAlloc;
    JPH::Reallocate = JoltRealloc;
    JPH::Free = JoltFree;
    JPH::AlignedAllocate = JoltAlignedAlloc;
    JPH::AlignedFree = JoltAlignedFree;
}

void JoltWorld::RegisterPhysics() const
{
    PhysicsWorld::RegisterAllocators(*memoryManager);
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void JoltWorld::UnregisterPhysics() const
{
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

JoltWorld::JoltWorld(Core::MemoryManager& memoryManager, enki::TaskScheduler* scheduler)
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

JoltWorld::~JoltWorld()
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

void JoltWorld::Step(float deltaTime)
{
    const auto error = static_cast<uint32_t>(physicsSystem.Update(deltaTime, PHYSICS_COLLISION_STEPS, &tempAllocator, jobSystem));
    const uint32_t newErrors = error & ~reportedUpdateErrors;
    if (newErrors != 0) {
        reportedUpdateErrors |= newErrors;
        SPDLOG_WARN("[PhysicsWorld::Step] Jolt dropped work this step (first occurrence):{}{}{}; bodies may interpenetrate",
                    (newErrors & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ManifoldCacheFull)) ? " manifold cache full" : "",
                    (newErrors & static_cast<uint32_t>(JPH::EPhysicsUpdateError::BodyPairCacheFull)) ? " body pair cache full" : "",
                    (newErrors & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ContactConstraintsFull)) ? " contact constraints full" : "");
    }
}

void JoltWorld::DrawDebug(Core::ViewFamilyDebugDraws* debugDraws, const glm::vec3& cameraPos, bool bUseFilter, bool bSleepColor)
{
#if JPH_DEBUG_RENDERER
    debugRenderer->SetDebugDraws(debugDraws, cameraPos);

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

PhysicsWorld::PhysicsWorld(Core::MemoryManager& memoryManager, enki::TaskScheduler* scheduler)
    : memoryManager(&memoryManager)
{
    void* mem = memoryManager.PhysicsAllocRaw(sizeof(JoltWorld), std::max(alignof(JoltWorld), size_t{64}));
    jolt = new(mem) JoltWorld(memoryManager, scheduler);
}

PhysicsWorld::~PhysicsWorld()
{
    jolt->~JoltWorld();
    memoryManager->PhysicsFree(jolt);
    jolt = nullptr;
}

void PhysicsWorld::Step(float deltaTime)
{
    jolt->Step(deltaTime);
}

void PhysicsWorld::ClearCollisionEvents()
{
    jolt->ClearCollisionEvents();
}

void PhysicsWorld::ClearActivationEvents()
{
    jolt->ClearActivationEvents();
}

void PhysicsWorld::DrawDebug(Core::ViewFamilyDebugDraws* debugDraws, const glm::vec3& cameraPos, bool bUseFilter, bool bSleepColor)
{
    jolt->DrawDebug(debugDraws, cameraPos, bUseFilter, bSleepColor);
}

Core::ArenaFixedVector<BodyHandle> PhysicsWorld::QueryBodiesInBox(Core::Arena* arena, const AABB& box) const
{
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
    jolt->GetPhysicsSystem().GetBroadPhaseQuery().CollideAABox(ToJolt(box), collector);
    auto bodies = Core::ArenaFixedVector<BodyHandle>(arena, std::max(collector.mHits.size(), size_t{1}));
    for (const JPH::BodyID& id : collector.mHits) { bodies.PushBack(ToHandle(id)); }
    return bodies;
}

AABB PhysicsWorld::GetWorldBounds(BodyHandle body) const
{
    return ToGLM(jolt->GetBodyInterface().GetTransformedShape(ToJolt(body)).GetWorldSpaceBounds());
}

AABB PhysicsWorld::GetLocalBounds(BodyHandle body) const
{
    const JPH::RefConst<JPH::Shape> shape = jolt->GetBodyInterface().GetShape(ToJolt(body));
    return shape ? ToGLM(shape->GetLocalBounds()) : AABB{};
}

Mat4 PhysicsWorld::GetCenterOfMassTransform(BodyHandle body) const
{
    return ToGLM(jolt->GetBodyInterface().GetCenterOfMassTransform(ToJolt(body)));
}

uint64_t PhysicsWorld::GetUserData(BodyHandle body) const
{
    return jolt->GetBodyInterface().GetUserData(ToJolt(body));
}

Vec3 PhysicsWorld::GetPosition(BodyHandle body) const
{
    return ToGLM(jolt->GetBodyInterface().GetPosition(ToJolt(body)));
}

Vec3 PhysicsWorld::GetCenterOfMassPosition(BodyHandle body) const
{
    return ToGLM(jolt->GetBodyInterface().GetCenterOfMassPosition(ToJolt(body)));
}

Vec3 PhysicsWorld::GetLinearVelocity(BodyHandle body) const
{
    return ToGLM(jolt->GetBodyInterface().GetLinearVelocity(ToJolt(body)));
}

Vec3 PhysicsWorld::GetAngularVelocity(BodyHandle body) const
{
    return ToGLM(jolt->GetBodyInterface().GetAngularVelocity(ToJolt(body)));
}

BodyState PhysicsWorld::GetBodyState(BodyHandle body) const
{
    const JPH::BodyInterface& bodyInterface = jolt->GetBodyInterface();
    const JPH::BodyID id = ToJolt(body);
    const JPH::EMotionType motionType = bodyInterface.GetMotionType(id);
    return {
        .motionType = motionType == JPH::EMotionType::Static ? MotionType::Static : motionType == JPH::EMotionType::Kinematic ? MotionType::Kinematic : MotionType::Dynamic,
        .objectLayer = bodyInterface.GetObjectLayer(id),
        .bAwake = bodyInterface.IsActive(id),
        .linearVelocity = ToGLM(bodyInterface.GetLinearVelocity(id)),
        .angularVelocity = ToGLM(bodyInterface.GetAngularVelocity(id)),
    };
}

bool PhysicsWorld::IsAwake(BodyHandle body) const
{
    return jolt->GetBodyInterface().IsActive(ToJolt(body));
}

uint32_t PhysicsWorld::GetAwakeBodyCount() const
{
    return jolt->GetPhysicsSystem().GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

BodyCounts PhysicsWorld::GetBodyCounts() const
{
    const JPH::BodyManager::BodyStats stats = jolt->GetPhysicsSystem().GetBodyStats();
    return {
        .total = stats.mNumBodies,
        .staticBodies = stats.mNumBodiesStatic,
        .kinematic = stats.mNumBodiesKinematic,
        .kinematicAwake = stats.mNumActiveBodiesKinematic,
        .dynamic = stats.mNumBodiesDynamic,
        .dynamicAwake = stats.mNumActiveBodiesDynamic,
    };
}

void PhysicsWorld::AddImpulse(BodyHandle body, const Vec3& impulse) const
{
    jolt->GetBodyInterface().AddImpulse(ToJolt(body), ToJolt(impulse));
}

void PhysicsWorld::AddTorque(BodyHandle body, const Vec3& torque) const
{
    jolt->GetBodyInterface().AddTorque(ToJolt(body), ToJolt(torque));
}

void PhysicsWorld::MakeDynamic(BodyHandle body, const Vec3& linearVelocity) const
{
    JPH::BodyInterface& bodyInterface = jolt->GetBodyInterface();
    bodyInterface.SetMotionType(ToJolt(body), JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
    bodyInterface.SetLinearVelocity(ToJolt(body), ToJolt(linearVelocity));
}

class IgnoredLayersFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit IgnoredLayersFilter(uint32_t ignored) : ignored(ignored) {}

    bool ShouldCollide(JPH::ObjectLayer layer) const override { return (ignored & Layers::Bit(layer)) == 0; }

private:
    uint32_t ignored;
};

bool PhysicsWorld::CastRay(const Vec3& origin, const Vec3& direction, CastHit& hit, BodyHandle ignore, uint32_t ignoredLayers) const
{
    const JPH::RRayCast ray{ToJoltR(origin), ToJolt(direction)};
    JPH::RayCastResult result;
    const IgnoredLayersFilter layerFilter(ignoredLayers);
    const JPH::IgnoreSingleBodyFilter bodyFilter(ToJolt(ignore));
    if (!jolt->GetPhysicsSystem().GetNarrowPhaseQuery().CastRay(ray, result, {}, layerFilter, bodyFilter)) { return false; }
    hit = {result.mFraction, ToHandle(result.mBodyID)};
    return true;
}

bool PhysicsWorld::CastSphere(float radius, const Vec3& origin, const Vec3& direction, CastHit& hit, BodyHandle ignore, uint32_t ignoredLayers) const
{
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const JPH::RShapeCast cast(&sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(ToJoltR(origin)), ToJolt(direction));
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const IgnoredLayersFilter layerFilter(ignoredLayers);
    const JPH::IgnoreSingleBodyFilter bodyFilter(ToJolt(ignore));
    jolt->GetPhysicsSystem().GetNarrowPhaseQuery().CastShape(cast, {}, JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);
    if (!collector.HadHit()) { return false; }
    hit = {collector.mHit.mFraction, ToHandle(collector.mHit.mBodyID2)};
    return true;
}

Core::ArenaFixedVector<BodyTouch> PhysicsWorld::QueryTouching(Core::Arena* arena, BodyHandle body, float maxSeparation, size_t capacity) const
{
    constexpr size_t MAX_SUB_SHAPES = 16;
    struct Touch
    {
        BodyTouch touch;
        Core::InlineVector<JPH::SubShapeID, MAX_SUB_SHAPES> subShapes{};
    };

    const JPH::BodyID id = ToJolt(body);
    const JPH::TransformedShape shape = jolt->GetBodyInterface().GetTransformedShape(id);
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = maxSeparation;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const JPH::IgnoreSingleBodyFilter ignoreSelf(id);
    jolt->GetPhysicsSystem().GetNarrowPhaseQuery().CollideShape(shape.mShape, shape.GetShapeScale(), shape.GetCenterOfMassTransform(), settings,
                                                                shape.GetCenterOfMassTransform().GetTranslation(), collector, {}, {}, ignoreSelf);

    auto touches = Core::ArenaFixedVector<Touch>(arena, std::max(capacity, size_t{1}));
    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        const BodyHandle other = ToHandle(hit.mBodyID2);
        Touch* touch = nullptr;
        for (Touch& t : touches) {
            if (t.touch.body == other) { touch = &t; }
        }
        if (!touch) {
            if (touches.Size() >= capacity) { continue; }
            touches.PushBack({.touch = {.body = other}});
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
} // Physics
