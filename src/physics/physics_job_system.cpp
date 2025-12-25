//
// Created by William on 2025-12-25.
//

#include "physics_job_system.h"

namespace Physics
{
PhysicsJobSystem::PhysicsJobSystem(enki::TaskScheduler* scheduler, uint32_t maxJobs, uint32_t inMaxBarriers)
    : scheduler(scheduler)
{
    Init(inMaxBarriers);

    mJobs.Init(maxJobs, maxJobs);
}

int32_t PhysicsJobSystem::GetMaxConcurrency() const
{
    return static_cast<int32_t>(scheduler->GetNumTaskThreads());
}

JPH::JobHandle PhysicsJobSystem::CreateJob(const char* inName, JPH::ColorArg inColor, const JobFunction& inJobFunction, JPH::uint32 inNumDependencies)
{
    // Copied from JobSystemThreadPool
    JPH_PROFILE_FUNCTION();

    // Spin-lock until index is available
    uint32_t index;
    for (;;) {
        index = mJobs.ConstructObject(inName, inColor, this, inJobFunction, inNumDependencies);
        if (index != AvailableJobs::cInvalidObjectIndex)
            break;
        JPH_ASSERT(false, "No jobs available!");
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    Job* job = &mJobs.Get(index);
    JobHandle handle(job);

    if (inNumDependencies == 0) {
        QueueJob(job);
    }

    return handle;
}

uint64_t PhysicsJobSystem::ResetTaskPool()
{
    const uint64_t out = mTaskIndex.load(std::memory_order_acquire);
    mTaskIndex = 0;
    return out;
}

void PhysicsJobSystem::QueueJob(Job* inJob)
{
    uint64_t idx = mTaskIndex.fetch_add(1) % MAX_PHYSICS_TASKS;
    while (!mTasks[idx].GetIsComplete()) {
        idx = mTaskIndex.fetch_add(1) % MAX_PHYSICS_TASKS;
    }
    mTasks[idx].Reset();
    inJob->AddRef();

    mTasks[idx].jobs.push_back(inJob);
    mTasks[idx].m_SetSize = 1;
    scheduler->AddTaskSetToPipe(&mTasks[idx]);
}

void PhysicsJobSystem::QueueJobs(Job** inJobs, JPH::uint inNumJobs)
{
    JPH_ASSERT(inNumJobs > 0);
    uint64_t idx = mTaskIndex.fetch_add(1) % MAX_PHYSICS_TASKS;
    while (!mTasks[idx].GetIsComplete()) {
        idx = mTaskIndex.fetch_add(1) % MAX_PHYSICS_TASKS;
    }

    // Batch jobs into single task
    mTasks[idx].Reset();
    for (uint32_t i = 0; i < inNumJobs; ++i) {
        inJobs[i]->AddRef();
        mTasks[idx].jobs.push_back(inJobs[i]);
    }
    mTasks[idx].m_SetSize = inNumJobs;
    scheduler->AddTaskSetToPipe(&mTasks[idx]);
}

void PhysicsJobSystem::FreeJob(Job* inJob)
{
    mJobs.DestructObject(inJob);
}
} // Physics