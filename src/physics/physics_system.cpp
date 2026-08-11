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
#include "core/containers/inline_string.h"
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
    , physicsArena(memoryManager.ArenaPool(), PHYSICS_TEMP_ALLOCATOR_SIZE, Core::AllocTag::Physics)
    , tempAllocator(physicsArena.Get().Data(), physicsArena.Get().GetCapacity())
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
    physicsSystem.Update(deltaTime, PHYSICS_COLLISION_STEPS, &tempAllocator, jobSystem);
}

void PhysicsSystem::DrawDebug(Core::ViewFamily* viewFamily, bool bUseFilter)
{
#if JPH_DEBUG_RENDERER
    debugRenderer->SetViewFamily(viewFamily);

    JPH::BodyManager::DrawSettings settings;
    settings.mDrawShape = true;
    settings.mDrawShapeWireframe = true;
    settings.mDrawShapeColor = JPH::BodyManager::EShapeColor::InstanceColor;

    if (bUseFilter) {
        physicsSystem.DrawBodies(settings, debugRenderer, debugDrawFilter);
    } else {
        physicsSystem.DrawBodies(settings, debugRenderer, nullptr);
    }
#endif
}

} // Physics
