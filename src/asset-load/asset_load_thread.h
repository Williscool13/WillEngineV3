//
// Created by William on 2025-12-17.
//

#ifndef WILL_ENGINE_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASSET_LOAD_THREAD_H
#include <bitset>
#include <memory>

#include <LockFreeQueue/LockFreeQueueCpp11.h>

#include "asset_load_config.h"
#include "asset-load-jobs/asset_load_job.h"
#include "asset_load_types.h"
#include "TaskScheduler.h"
#include "render/resource_manager.h"

template<typename T>
using LockFreeQueue = LockFreeQueueCpp11<T>;

namespace Render
{
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class PipelineLoadJob;
class TextureLoadJob;
class WillModelLoadJob;
class AssetLoadJob;

struct AssetLoadSlot
{
    std::string name;
    AssetLoadState loadState{AssetLoadState::Unassigned};
    AssetLoadJob* job;
    AssetType type{AssetType::None};
    std::chrono::steady_clock::time_point startTime;
    uint32_t uploadCount = 0;
};

/**
 * Asset loading thread class, responsible for asynchronously loading any assets necessary for the game. Crosses multiple engine boundaries by nature.
 * \n Will only ever assign 4 tasks at a time
 */
class AssetLoadThread
{
public:
    AssetLoadThread();

    AssetLoadThread(enki::TaskScheduler* scheduler, Render::VulkanContext* context, Render::ResourceManager* resourceManager);

    ~AssetLoadThread();

    void Start();

    void RequestShutdown();

    void Join() const;

public:
    void RequestLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr);

    bool ResolveLoads(WillModelComplete& modelComplete);

    void RequestUnLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr);

    bool ResolveUnload(WillModelComplete& modelComplete);

    void RequestTextureLoad(Engine::TextureHandle textureHandle, Render::Texture* texturePtr);

    bool ResolveTextureLoads(TextureComplete& textureComplete);

    void RequestTextureUnload(Engine::TextureHandle textureHandle, Render::Texture* texturePtr);

    bool ResolveTextureUnload(TextureComplete& textureComplete);

    void RequestPipelineLoad(const std::string& name, Render::PipelineData* data);

    bool ResolvePipelineLoads(PipelineComplete& pipelineComplete);

    Render::Sampler CreateSampler(const VkSamplerCreateInfo& samplerCreateInfo) const;

private: // Threading
    void ThreadMain();

private:
    Render::VulkanContext* context{};
    Render::ResourceManager* resourceManager{};
    enki::TaskScheduler* scheduler{};

private: // Threading
    std::atomic<bool> bShouldExit{false};
    std::unique_ptr<enki::LambdaPinnedTask> pinnedTask{};

    LockFreeQueue<WillModelLoadRequest> modelLoadQueue{WILL_MODEL_LOAD_QUEUE_COUNT};
    LockFreeQueue<WillModelComplete> modelCompleteLoadQueue{WILL_MODEL_LOAD_QUEUE_COUNT};
    LockFreeQueue<WillModelLoadRequest> modelUnloadQueue{WILL_MODEL_LOAD_QUEUE_COUNT};
    LockFreeQueue<WillModelComplete> modelCompleteUnloadQueue{WILL_MODEL_LOAD_QUEUE_COUNT};

    LockFreeQueue<TextureLoadRequest> textureLoadQueue{TEXTURE_LOAD_QUEUE_COUNT};
    LockFreeQueue<TextureComplete> textureCompleteLoadQueue{TEXTURE_LOAD_QUEUE_COUNT};
    LockFreeQueue<TextureLoadRequest> textureUnloadQueue{TEXTURE_LOAD_QUEUE_COUNT};
    LockFreeQueue<TextureComplete> textureCompleteUnloadQueue{TEXTURE_LOAD_QUEUE_COUNT};

    LockFreeQueue<PipelineLoadRequest> pipelineLoadQueue{PIPELINE_LOAD_QUEUE_COUNT};
    LockFreeQueue<PipelineComplete> pipelineCompleteLoadQueue{PIPELINE_LOAD_QUEUE_COUNT};

    std::array<AssetLoadSlot, MAX_ASSET_LOAD_JOB_COUNT> assetLoadSlots{};
    std::bitset<MAX_ASSET_LOAD_JOB_COUNT> activeSlotMask{0};

    std::vector<std::unique_ptr<WillModelLoadJob> > willModelJobs{};
    std::bitset<WILL_MODEL_JOB_COUNT> willModelJobActive;
    std::vector<std::unique_ptr<TextureLoadJob>> textureJobs;
    std::bitset<TEXTURE_JOB_COUNT> textureJobActive;
    std::vector<std::unique_ptr<PipelineLoadJob>> pipelineJobs;
    std::bitset<PIPELINE_JOB_COUNT> pipelineJobActive;

    VkCommandPool commandPool{};
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_THREAD_H
