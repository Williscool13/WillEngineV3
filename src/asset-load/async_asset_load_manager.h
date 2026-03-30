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

#include "asset_load_config.h"
#include "asset-load-jobs/audio_load_slot.h"
#include "asset-load-jobs/pipeline_load_slot.h"
#include "asset-load-jobs/static_model_load_slot.h"
#include "asset-load-jobs/procedural_model_load_slot.h"
#include "asset-load-jobs/texture_load_slot.h"
#include "asset-load-jobs/cubemap_load_slot.h"
#include "core/memory/lock_free_handle_allocator.h"
#include "engine/resources/sampler/sampler.h"

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

struct StaticModelLoadRequest
{
    Engine::StaticModel* model;
};

struct StaticModelLoadComplete
{
    Engine::StaticModel* model;
    bool bSuccess;
};

struct TextureLoadRequest
{
    Engine::Texture* texture;
};

struct TextureLoadComplete
{
    Engine::Texture* texture;
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

struct SamplerLoadRequest
{
    Engine::Sampler* sampler;
};

struct SamplerLoadComplete
{
    Engine::Sampler* sampler;
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
    void RequestModelLoad(Engine::StaticModel* model);

    bool TryDequeueModelComplete(StaticModelLoadComplete& outResult);

    // Procedural model loading
    void RequestProceduralModelLoad(Engine::StaticModel* model);

    bool TryDequeueProceduralModelComplete(StaticModelLoadComplete& outResult);


    // Texture loading
    void RequestTextureLoad(Engine::Texture* texture);

    bool TryDequeueTextureComplete(TextureLoadComplete& outResult);

    // Cubemap loading
    void RequestCubemapLoad(Render::Cubemap* cubemap);

    bool TryDequeueCubemapComplete(CubemapLoadComplete& outResult);

    // Sampler loading
    void RequestSamplerLoad(Engine::Sampler* sampler);

    bool TryDequeueSamplerComplete(SamplerLoadComplete& outResult);


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
    Core::LockFreeHandleAllocator<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadAllocator;
    std::array<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadSlots;
    moodycamel::ConcurrentQueue<AudioLoadRequest> audioRequestQueue;
    moodycamel::ConcurrentQueue<AudioLoadComplete> audioLoadCompleteQueue;

    // Pipeline loading
    Core::LockFreeHandleAllocator<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadAllocator;
    std::array<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadSlots;
    moodycamel::ConcurrentQueue<PipelineLoadRequest> pipelineRequestQueue;
    moodycamel::ConcurrentQueue<PipelineLoadComplete> pipelineLoadCompleteQueue;

    // Model Loading
    Core::LockFreeHandleAllocator<StaticModelLoadSlot, MODEL_JOB_COUNT> modelLoadAllocator;
    std::array<StaticModelLoadSlot, MODEL_JOB_COUNT> modelLoadSlots;
    moodycamel::ConcurrentQueue<StaticModelLoadRequest> modelRequestQueue;
    moodycamel::ConcurrentQueue<StaticModelLoadComplete> modelLoadCompleteQueue;

    // Procedural Model Loading
    Core::LockFreeHandleAllocator<ProceduralModelLoadSlot, PROCEDURAL_MODEL_JOB_COUNT> proceduralModelLoadAllocator;
    std::array<ProceduralModelLoadSlot, PROCEDURAL_MODEL_JOB_COUNT> proceduralModelLoadSlots;
    moodycamel::ConcurrentQueue<StaticModelLoadRequest> proceduralModelRequestQueue;
    moodycamel::ConcurrentQueue<StaticModelLoadComplete> proceduralModelLoadCompleteQueue;

    // Texture Loading
    Core::LockFreeHandleAllocator<TextureLoadSlot, TEXTURE_JOB_COUNT> textureLoadAllocator;
    std::array<TextureLoadSlot, TEXTURE_JOB_COUNT> textureLoadSlots;
    moodycamel::ConcurrentQueue<TextureLoadRequest> textureRequestQueue;
    moodycamel::ConcurrentQueue<TextureLoadComplete> textureLoadCompleteQueue;

    // Cubemap Loading
    Core::LockFreeHandleAllocator<CubemapLoadSlot, CUBEMAP_JOB_COUNT> cubemapLoadAllocator;
    std::array<CubemapLoadSlot, CUBEMAP_JOB_COUNT> cubemapLoadSlots;
    moodycamel::ConcurrentQueue<CubemapLoadRequest> cubemapRequestQueue;
    moodycamel::ConcurrentQueue<CubemapLoadComplete> cubemapLoadCompleteQueue;

    // Sampler loading (processed inline in ThreadMain, no task slot needed)
    moodycamel::ConcurrentQueue<SamplerLoadRequest> samplerRequestQueue;
    moodycamel::ConcurrentQueue<SamplerLoadComplete> samplerLoadCompleteQueue;

    // GPU Uploads
    moodycamel::ConcurrentQueue<GPUDispatchRequest> gpuDispatchQueue;
    std::vector<GPUDispatchRequest> dispatchBatch;
    std::vector<VkFence> fences;
    Core::LockFreeHandleAllocator<UploadStaging, GPU_DISPATCH_COUNT> uploadStagingAllocator{};
    std::array<UploadStaging, GPU_DISPATCH_COUNT> uploadStagings{};

    void OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle);

    void OnPipelineLoadComplete(bool success, PipelineSlotHandle slotHandle);

    void OnModelLoadComplete(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnProceduralModelLoadComplete(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnTextureLoadComplete(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnCubemapComplete(bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);
};
} // AssetLoad
#endif //WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
