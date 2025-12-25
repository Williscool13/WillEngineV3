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


#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine)
{
    std::string msg = fmt::format("JPH Assert Failed: {} | {} ({}:{})", inExpression, inMessage, inFile, inLine);
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


namespace Physics
{
PhysicsSystem::PhysicsSystem() = default;

PhysicsSystem::PhysicsSystem(enki::TaskScheduler* scheduler)
    : scheduler(scheduler)
{
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    tempAllocator= std::make_unique<JPH::TempAllocatorImpl>(PHYSICS_TEMP_ALLOCATOR_SIZE);
    jobSystem = std::make_unique<PhysicsJobSystem>(scheduler, MAX_PHYSICS_JOBS, 8);

    physicsSystem.Init(MAX_PHYSICS_BODIES, PHYSICS_BODY_MUTEX_COUNT,
                       MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS,
                       broadPhaseLayerInterface,
                       objectVsBroadPhaseLayerFilter,
                       objectLayerPairFilter);
    physicsSystem.SetBodyActivationListener(&bodyActivationListener);
    physicsSystem.SetContactListener(&contactListener);

    SPDLOG_INFO("Physics System initialized | Bodies: {} | Mutexes: {} | Body Pairs: {} | Contacts: {} | Jobs: {} | Barriers: 8",
                MAX_PHYSICS_BODIES, PHYSICS_BODY_MUTEX_COUNT, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS, MAX_PHYSICS_JOBS);
}

PhysicsSystem::~PhysicsSystem()
{
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
};

void PhysicsSystem::Step(float deltaTime)
{
    constexpr int cCollisionSteps = 1;
    physicsSystem.Update(deltaTime, cCollisionSteps, tempAllocator.get(), jobSystem.get());
}
} // Physics
