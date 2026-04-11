//
// Created by William on 2025-12-25.
//

#include "physics_system.h"

#include <cstdarg>
#include <Jolt/RegisterTypes.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "body_activation_listener.h"
#include "contact_listener.h"
#include "physics_job_system.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"


#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine)
{
    char msg[512];
    snprintf(msg, sizeof(msg), "JPH Assert Failed: %s | %s (%s:%u)", inExpression, inMessage, inFile, inLine);
    SPDLOG_ERROR("{}", msg);
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
// Routed through the physics TLSF pool (mutex-enabled for thread safety).

static Core::TlsfAllocator* sPhysicsAlloc = nullptr;

// All three routed through memalign to guarantee JPH_RVECTOR_ALIGNMENT.
// Jolt's operator new calls JPH::Allocate directly for alignas(16) types (not over-aligned
// on MSVC x64), so JoltAlloc must return JPH_RVECTOR_ALIGNMENT-aligned memory.
// Realloc free+allocs to preserve alignment (tlsf_realloc doesn't guarantee it).

static void* JoltAlloc(size_t size)
{
    return sPhysicsAlloc->AlignedAlloc(size, JPH_RVECTOR_ALIGNMENT, Core::AllocTag::Physics);
}

static void* JoltRealloc(void* ptr, size_t oldSize, size_t newSize)
{
    void* newPtr = sPhysicsAlloc->AlignedAlloc(newSize, JPH_RVECTOR_ALIGNMENT, Core::AllocTag::Physics);
    if (ptr) {
        memcpy(newPtr, ptr, oldSize < newSize ? oldSize : newSize);
        sPhysicsAlloc->AlignedFree(ptr);
    }
    return newPtr;
}

static void JoltFree(void* ptr)
{
    sPhysicsAlloc->AlignedFree(ptr);
}

static void* JoltAlignedAlloc(size_t size, size_t alignment)
{
    return sPhysicsAlloc->AlignedAlloc(size, alignment, Core::AllocTag::Physics);
}

static void JoltAlignedFree(void* ptr)
{
    sPhysicsAlloc->AlignedFree(ptr);
}

namespace Physics
{
PhysicsSystem::PhysicsSystem() = default;

void PhysicsSystem::RegisterAllocators(Core::MemoryManager& memoryManager)
{
    sPhysicsAlloc = &memoryManager.Physics();
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

PhysicsSystem::PhysicsSystem(Core::MemoryManager& memoryManager, enki::TaskScheduler* scheduler)
    : memoryManager(&memoryManager)
    , scheduler(scheduler)
    , tempAllocator(memoryManager.PhysicsArena().Data(), memoryManager.PhysicsArena().GetCapacity())
{
    RegisterPhysics();

    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    void* jobMem = memoryManager.PhysicsAllocRaw(sizeof(PhysicsJobSystem));
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
    void* mem0 = memoryManager.PhysicsAllocRaw(sizeof(DebugRenderer));
    debugRenderer = new(mem0) DebugRenderer();
    void* mem1 = memoryManager.PhysicsAllocRaw(sizeof(DebugDrawFilter));
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
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
};

void PhysicsSystem::Step(float deltaTime)
{
    physicsSystem.Update(deltaTime, PHYSICS_COLLISION_STEPS, &tempAllocator, jobSystem);
}

void PhysicsSystem::DrawDebug(Core::ViewFamily* viewFamily, bool bUseFilter)
{
#if JPH_DEBUG_RENDERER
    debugRenderer->SetViewFamily(viewFamily);

    JPH::BodyManager::DrawSettings settings;
    settings.mDrawShape = true;
    settings.mDrawShapeWireframe = true;
    settings.mDrawShapeColor = JPH::BodyManager::EShapeColor::MotionTypeColor;

    if (bUseFilter) {
        physicsSystem.DrawBodies(settings, debugRenderer, debugDrawFilter);
    } else {
        physicsSystem.DrawBodies(settings, debugRenderer, nullptr);
    }
#endif
}

} // Physics
