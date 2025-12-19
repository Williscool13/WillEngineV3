//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_I_LOADABLE_ASSET_H
#define WILL_ENGINE_I_LOADABLE_ASSET_H

#include <atomic>
#include <TaskScheduler.h>

/*namespace AssetLoad
{
struct LoadModelTask;

class ILoadableAsset
{
public:
    enum class LoadState
    {
        Unloaded,
        TaskExecuting,
        ThreadExecuting,
        Loaded,
        Failed
    };

    enum class TaskState
    {
        InProgress,
        Success,
        Failed
    };

    ILoadableAsset() = default;

    virtual ~ILoadableAsset() = default;

    ILoadableAsset(const ILoadableAsset&) = delete;

    ILoadableAsset& operator=(const ILoadableAsset&) = delete;

    ILoadableAsset(ILoadableAsset&&) = delete;

    ILoadableAsset& operator=(ILoadableAsset&&) = delete;

    virtual ExecuteState TaskExecute(enki::TaskScheduler* scheduler, LoadModelTask* task) = 0;

    virtual void TaskImplementation() = 0;

    /**
     * Return true if all gpu resources have finished uploading (staging finished)
     * @return
     #1#
    virtual ExecuteState ThreadExecute() = 0;
};
}*/


#endif //WILL_ENGINE_I_LOADABLE_ASSET_H
