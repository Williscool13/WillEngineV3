//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_JOB_SYSTEM_H
#define WILL_ENGINE_PHYSICS_JOB_SYSTEM_H

#include <array>
#include <JoltPhysics/Jolt/Jolt.h>
#include <JoltPhysics/Jolt/Core/FixedSizeFreeList.h>
#include <JoltPhysics/Jolt/Core/JobSystemWithBarrier.h>
#include <enkiTS/src/TaskScheduler.h>

#include "physics_config.h"

namespace Physics
{
class PhysicsJobSystem : public JPH::JobSystemWithBarrier
{
    struct PhysicsJobTask final : enki::ITaskSet
    {
        std::vector<Job*> jobs;

        PhysicsJobTask()
        {
            jobs.reserve(16);
            m_SetSize = 0;
        }

        void ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_) override
        {
            for (uint32_t i = range_.start; i < range_.end; ++i) {
                jobs[i]->Execute();
                jobs[i]->Release();
            }
        }

        void Reset()
        {
            jobs.clear();
            m_SetSize = 0;
        }
    };

public:
    PhysicsJobSystem() = default;

    PhysicsJobSystem(enki::TaskScheduler* scheduler, uint32_t maxJobs, uint32_t inMaxBarriers);

    ~PhysicsJobSystem() override = default;

    int32_t GetMaxConcurrency() const override;

    JobHandle CreateJob(const char* inName, JPH::ColorArg inColor, const JobFunction& inJobFunction, JPH::uint32 inNumDependencies) override;

    uint64_t ResetTaskPool();

protected:
    void QueueJob(Job* inJob) override;

    void QueueJobs(Job** inJobs, JPH::uint inNumJobs) override;

    void FreeJob(Job* inJob) override;

private:
    enki::TaskScheduler* scheduler;

    /// Array of jobs (fixed size)
    using AvailableJobs = JPH::FixedSizeFreeList<Job>;
    AvailableJobs mJobs;
    std::array<PhysicsJobTask, MAX_PHYSICS_TASKS> mTasks;
    std::atomic<uint64_t> mTaskIndex{0};
};
} // Physics

#endif //WILL_ENGINE_PHYSICS_JOB_SYSTEM_H
