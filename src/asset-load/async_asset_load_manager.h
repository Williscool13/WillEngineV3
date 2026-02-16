//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H

#include <atomic>
#include <array>
#include <semaphore>

#include <TaskScheduler.h>
#include <concurrentqueue/concurrentqueue.h>

#include "asset-load-jobs/audio_load_slot.h"
#include "asset-load-jobs/pipeline_load_slot.h"
#include "asset-load-jobs/will_model_load_slot.h"
#include "asset-load-jobs/texture_load_slot.h"
#include "asset-load-jobs/cubemap_load_slot.h"
#include "core/allocators/lock_free_handle_allocator.h"

namespace AssetLoad
{
struct AudioLoadRequest
{
    Audio::WillAudio* audioEntry;
};

struct AudioLoadComplete
{
    Audio::WillAudio* audioEntry;
    bool bSuccess;
};

struct PipelineLoadRequest
{
    Render::PipelineData* entry;
};

struct PipelineLoadComplete
{
    Render::PipelineData* pipelineData;
    bool bSuccess;
};

struct GPUDispatchRequest
{
    VkCommandBuffer cmd;
    VkFence fence;
    std::binary_semaphore* completionSignal;
};

struct WillModelLoadRequest
{
    Render::WillModel* model;
};

struct WillModelLoadComplete
{
    Render::WillModel* model;
    bool bSuccess;
};

struct TextureLoadRequest
{
    Render::Texture* texture;
};

struct TextureLoadComplete
{
    Render::Texture* texture;
    bool bSuccess;
};

struct CubemapLoadRequest
{
    Render::Cubemap* cubemap;
};

struct CubemapLoadComplete
{
    Render::Cubemap* cubemap;
    bool bSuccess;
};

class AsyncAssetLoadManager
{
public:
    AsyncAssetLoadManager(Render::VulkanContext* context,
                          Render::ResourceManager* resourceManager,
                          VkPipelineCache pipelineCache);

    ~AsyncAssetLoadManager();

    AsyncAssetLoadManager(const AsyncAssetLoadManager&) = delete;

    AsyncAssetLoadManager& operator=(const AsyncAssetLoadManager&) = delete;

    AsyncAssetLoadManager(AsyncAssetLoadManager&&) = delete;

    AsyncAssetLoadManager& operator=(AsyncAssetLoadManager&&) = delete;

    void ThreadMain();

    void GPUDispatchThreadMain();

    void Join();

    // Audio Loading
    void RequestAudioLoad(Audio::WillAudio* audioEntry);

    bool TryDequeueAudioComplete(AudioLoadComplete& outResult);

    // Pipeline loading
    void RequestPipelineLoad(Render::PipelineData* pipelineData);

    bool TryDequeuePipelineComplete(PipelineLoadComplete& outResult);

    // Model loading
    void RequestModelLoad(Render::WillModel* model);

    bool TryDequeueModelComplete(WillModelLoadComplete& outResult);

    // Texture loading
    void RequestTextureLoad(Render::Texture* texture);

    bool TryDequeueTextureComplete(TextureLoadComplete& outResult);

    // Cubemap loading
    void RequestCubemapLoad(Render::Cubemap* cubemap);

    bool TryDequeueCubemapComplete(CubemapLoadComplete& outResult);


    [[nodiscard]] uint32_t GetActiveAudioLoadCount() const
    {
        return audioLoadAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActivePipelineLoadCount() const
    {
        return pipelineLoadAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActiveModelLoadCount() const
    {
        return modelLoadAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActiveTextureLoadCount() const
    {
        return textureLoadAllocator.GetCount();
    }

    void QueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal);

private:
    std::unique_ptr<enki::TaskScheduler> assetLoadScheduler{};
    Render::VulkanContext* context;
    Render::ResourceManager* resourceManager;
    VkPipelineCache pipelineCache;
    std::atomic<bool> bShouldExit{false};

    std::jthread thisThread;
    std::atomic<uint32_t> workCounter{0};
    std::mutex wakeMutex;
    std::condition_variable wakeCV;

    std::jthread gpuDispatchThread;
    std::atomic<uint32_t> gpuDispatchWorkCounter{0};
    std::mutex gpuDispatchWakeMutex;
    std::condition_variable gpuDispatchWakeCV;

    // Audio loading
    moodycamel::ConcurrentQueue<AudioLoadRequest> audioRequestQueue;
    Core::LockFreeHandleAllocator<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadAllocator;
    std::array<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadSlots;
    moodycamel::ConcurrentQueue<AudioLoadComplete> audioLoadCompleteQueue;

    // Pipeline loading
    moodycamel::ConcurrentQueue<PipelineLoadRequest> pipelineRequestQueue;
    Core::LockFreeHandleAllocator<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadAllocator;
    std::array<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadSlots;
    moodycamel::ConcurrentQueue<PipelineLoadComplete> pipelineLoadCompleteQueue;

    // Model Loading
    moodycamel::ConcurrentQueue<WillModelLoadRequest> modelRequestQueue;
    Core::LockFreeHandleAllocator<WillModelLoadSlot, MODEL_JOB_COUNT> modelLoadAllocator;
    std::array<WillModelLoadSlot, MODEL_JOB_COUNT> modelLoadSlots;
    moodycamel::ConcurrentQueue<WillModelLoadComplete> modelLoadCompleteQueue;

    // Texture Loading
    moodycamel::ConcurrentQueue<TextureLoadRequest> textureRequestQueue;
    Core::LockFreeHandleAllocator<TextureLoadSlot, TEXTURE_JOB_COUNT> textureLoadAllocator;
    std::array<TextureLoadSlot, TEXTURE_JOB_COUNT> textureLoadSlots;
    moodycamel::ConcurrentQueue<TextureLoadComplete> textureLoadCompleteQueue;

    // Cubemap Loading
    moodycamel::ConcurrentQueue<CubemapLoadRequest> cubemapRequestQueue;
    Core::LockFreeHandleAllocator<CubemapLoadSlot, CUBEMAP_JOB_COUNT> cubemapLoadAllocator;
    std::array<CubemapLoadSlot, CUBEMAP_JOB_COUNT> cubemapLoadSlots;
    moodycamel::ConcurrentQueue<CubemapLoadComplete> cubemapLoadCompleteQueue;

    // GPU Uploads
    moodycamel::ConcurrentQueue<GPUDispatchRequest> gpuDispatchQueue;
    std::vector<GPUDispatchRequest> dispatchBatch;
    std::vector<VkFence> fences;
    Core::LockFreeHandleAllocator<UploadStaging, GPU_DISPATCH_COUNT> uploadStagingAllocator{};
    std::array<UploadStaging, GPU_DISPATCH_COUNT> uploadStagings{};

    void OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle);

    void OnPipelineLoadComplete(bool success, PipelineSlotHandle slotHandle);

    void OnModelLoadComplete(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnTextureLoadComplete(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnCubemapComplete(bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);
};
} // AssetLoad
#endif //WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
