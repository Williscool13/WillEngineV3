//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_I_LOADABLE_ASSET_H
#define WILL_ENGINE_I_LOADABLE_ASSET_H

namespace AssetLoad
{
class ILoadableAsset
{
public:
    enum class LoadState
    {
        Idle,
        TaskExecuting,
        TaskComplete,
        ThreadComplete,
        Failed
    };

    virtual ~ILoadableAsset() = default;

    virtual void TaskExecute() = 0;

    virtual void ThreadExecute() = 0;

    LoadState GetState() const
    {
        return state.load(std::memory_order_acquire);
    }

protected:
    void SetState(LoadState newState)
    {
        state.store(newState, std::memory_order_release);
    }

    std::atomic<LoadState> state{LoadState::Idle};
};
}


#endif //WILL_ENGINE_I_LOADABLE_ASSET_H
