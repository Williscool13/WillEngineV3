//
// Created by William on 2025-12-17.
//

#ifndef WILL_ENGINE_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASSET_LOAD_THREAD_H
#include <bitset>
#include <memory>

#include <LockFreeQueue/LockFreeQueueCpp11.h>

#include "asset_load_config.h"
#include "asset_load_types.h"
#include "TaskScheduler.h"
#include "will_model_loader.h"
#include "render/vulkan/vk_resource_manager.h"

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

    void Join() const;

public:
    void RequestLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr);

    bool ResolveLoads(WillModelComplete& modelComplete);

private: // Threading
    void ThreadMain();

private:
    Render::VulkanContext* context{};
    Render::ResourceManager* resourceManager{};
    enki::TaskScheduler* scheduler{};

private: // Threading
    std::atomic<bool> bShouldExit{false};
    std::unique_ptr<enki::LambdaPinnedTask> pinnedTask{};

    LockFreeQueue<WillModelLoadRequest> modelLoadQueue{MODEL_LOAD_QUEUE_COUNT};
    LockFreeQueue<WillModelComplete> modelCompleteQueue{MODEL_LOAD_QUEUE_COUNT};

    std::bitset<ASSET_LOAD_ASYNC_COUNT> loaderActive;
    std::array<WillModelLoader, ASSET_LOAD_ASYNC_COUNT> assetLoadSlots{};

    VkCommandPool commandPool{};
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_THREAD_H
