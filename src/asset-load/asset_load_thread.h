//
// Created by William on 2025-12-17.
//

#ifndef WILL_ENGINE_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASSET_LOAD_THREAD_H
#include <memory>
#include <unordered_map>

#include <LockFreeQueue/LockFreeQueueCpp11.h>

#include "asset_load_config.h"
#include "asset_load_types.h"
#include "i_loadable_asset.h"
#include "TaskScheduler.h"
#include "core/allocators/free_list.h"
#include "core/allocators/handle_allocator.h"

template<typename T>
using LockFreeQueue = LockFreeQueueCpp11<T>;

namespace Render
{
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
/**
 * Asset loading thread class, responsible for asynchronously loading any assets necessary for the game. Crosses multiple engine boundaries by nature.
 * \n Will only ever assign 4 tasks at a time
 */
class AssetLoadThread
{
public:
    AssetLoadThread();

    ~AssetLoadThread();

    void Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager);

    void Start();

    void RequestShutdown();

    void Join();

private: // Threading
    void ThreadMain();

private:
    Render::VulkanContext* context{};
    Render::ResourceManager* resourceManager{};
    enki::TaskScheduler* scheduler{};

private: // Threading
    std::atomic<bool> bShouldExit{false};
    std::unique_ptr<enki::LambdaPinnedTask> pinnedTask{};

    LockFreeQueue<AssetLoadRequest> requestQueue{ASSET_LOAD_QUEUE_COUNT};
    LockFreeQueue<AssetLoadComplete> completeQueue{ASSET_LOAD_QUEUE_COUNT};

    std::vector<AssetLoadInProgress> modelsInProgress{};

private:
    VkCommandPool commandPool{};
    std::array<UploadStaging, ASSET_LOAD_ASYNC_COUNT> uploadStagingDatas;
    Core::HandleAllocator<UploadStaging, ASSET_LOAD_ASYNC_COUNT> uploadStagingHandleAllocator{};
    std::vector<UploadStagingHandle> activeUploadHandles;

private:
    Core::FreeList<WillModelHandle, MAX_LOADED_MODELS> models;
    std::unordered_map<std::filesystem::path, WillModelHandle> pathToHandle;


    struct LoadTask: enki::ITaskSet
    {
        ILoadableAsset* loadableAsset{nullptr};

        explicit LoadTask()
            : ITaskSet(1)
        {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
        {
            if (loadableAsset) {
                loadableAsset->TaskExecute();
            }
        }
    };
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_THREAD_H
