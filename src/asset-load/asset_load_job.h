//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_ASSET_LOAD_JOB_H
#define WILL_ENGINE_ASSET_LOAD_JOB_H
#include "TaskScheduler.h"

namespace AssetLoad
{
enum class AssetType
{
    None,
    WillModel,
    Texture,
    // AudioClip
};
enum class AssetLoadState
{
    Unassigned,
    Idle,
    TaskExecuting,
    ThreadExecuting,
    Loaded,
    Failed
};

enum class TaskState
{
    NotStarted,
    InProgress,
    Complete,
    Failed,
};

enum class ThreadState
{
    InProgress,
    Complete,
};

class AssetLoadJob
{
public:
    virtual ~AssetLoadJob() = default;

    virtual TaskState TaskExecute(enki::TaskScheduler* scheduler) = 0;

    virtual bool PreThreadExecute() = 0;

    /**
     * Can be called multiple times. Will attempt to call this again if it returns ThreadState::InProgress
     * @return
     */
    virtual ThreadState ThreadExecute() = 0;

    virtual bool PostThreadExecute() = 0;

    virtual void Reset() = 0;
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_JOB_H
