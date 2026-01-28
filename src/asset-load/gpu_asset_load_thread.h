//
// Created by William on 2025-12-17.
//

#ifndef WILL_ENGINE_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASSET_LOAD_THREAD_H
#include <bitset>
#include <memory>

#include "asset_load_config.h"
#include "asset-load-jobs/asset_load_job.h"
#include "asset_load_types.h"
#include "TaskScheduler.h"
#include "concurrentqueue/concurrentqueue.h"
#include "render/resource_manager.h"

namespace Render
{
class PipelineManager;
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class PipelineLoadJob;
class TextureLoadJob;
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
class GpuAssetUploadThread
{
public:
    GpuAssetUploadThread();

    GpuAssetUploadThread(enki::TaskScheduler* scheduler, Render::VulkanContext* context, Render::ResourceManager* resourceManager, Render::PipelineManager* pipelineManager);

    ~GpuAssetUploadThread();

    void Start();

    void RequestShutdown();

    void Join();

public: // Textures
    void RequestTextureLoad(Engine::TextureHandle textureHandle, Render::Texture* texturePtr);

    void RequestTextureUnload(Engine::TextureHandle textureHandle, Render::Texture* texturePtr);

    bool ResolveTextureLoads(TextureComplete& textureComplete);

    bool ResolveTextureUnload(TextureComplete& textureComplete);


    Render::Sampler CreateSampler(const VkSamplerCreateInfo& samplerCreateInfo) const;

private: // Threading
    void ThreadMain();

private:
    Render::VulkanContext* context{};
    Render::ResourceManager* resourceManager{};
    enki::TaskScheduler* scheduler{};

    std::jthread thisThread;
    std::atomic<bool> bShouldExit{false};
    std::atomic<uint32_t> workCounter{0};
    std::mutex wakeMutex;
    std::condition_variable wakeCV;

private: // Threading
    moodycamel::ConcurrentQueue<TextureLoadRequest> textureLoadQueue;
    moodycamel::ConcurrentQueue<TextureComplete> textureCompleteLoadQueue;
    moodycamel::ConcurrentQueue<TextureLoadRequest> textureUnloadQueue;
    moodycamel::ConcurrentQueue<TextureComplete> textureCompleteUnloadQueue;

    std::array<AssetLoadSlot, MAX_ASSET_LOAD_JOB_COUNT> assetLoadSlots{};
    std::bitset<MAX_ASSET_LOAD_JOB_COUNT> activeSlotMask{0};

    std::vector<std::unique_ptr<TextureLoadJob>> textureJobs;
    std::bitset<TEXTURE_JOB_COUNT> textureJobActive;

    VkCommandPool commandPool{};
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_THREAD_H
