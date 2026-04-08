//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H

#include <atomic>
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
#include "core/containers/array.h"
#include "core/memory/lock_free_handle_allocator.h"
#include "engine/resources/sampler/sampler.h"

namespace AssetLoad
{
class AsyncAssetLoadManager
{
public:
    AsyncAssetLoadManager(Core::MemoryManager& memoryManager,
                          Render::VulkanContext* context,
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
    enki::TaskScheduler* assetLoadScheduler{};
    Core::MemoryManager* memoryManager{};
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
    Core::Array<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadSlots;
    moodycamel::ConcurrentQueue<AudioLoadRequest> audioRequestQueue;
    moodycamel::ConcurrentQueue<AudioLoadComplete> audioLoadCompleteQueue;

    // Pipeline loading
    Core::LockFreeHandleAllocator<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadAllocator;
    Core::Array<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadSlots;
    moodycamel::ConcurrentQueue<PipelineLoadRequest> pipelineRequestQueue;
    moodycamel::ConcurrentQueue<PipelineLoadComplete> pipelineLoadCompleteQueue;

    // Model Loading
    Core::LockFreeHandleAllocator<StaticModelLoadSlot, MODEL_JOB_COUNT> modelLoadAllocator;
    Core::Array<StaticModelLoadSlot, MODEL_JOB_COUNT> modelLoadSlots;
    moodycamel::ConcurrentQueue<StaticModelLoadRequest> modelRequestQueue;
    moodycamel::ConcurrentQueue<StaticModelLoadComplete> modelLoadCompleteQueue;

    // Procedural Model Loading
    Core::LockFreeHandleAllocator<ProceduralModelLoadSlot, PROCEDURAL_MODEL_JOB_COUNT> proceduralModelLoadAllocator;
    Core::Array<ProceduralModelLoadSlot, PROCEDURAL_MODEL_JOB_COUNT> proceduralModelLoadSlots;
    moodycamel::ConcurrentQueue<StaticModelLoadRequest> proceduralModelRequestQueue;
    moodycamel::ConcurrentQueue<StaticModelLoadComplete> proceduralModelLoadCompleteQueue;

    // Texture Loading
    Core::LockFreeHandleAllocator<TextureLoadSlot, TEXTURE_JOB_COUNT> textureLoadAllocator;
    Core::Array<TextureLoadSlot, TEXTURE_JOB_COUNT> textureLoadSlots;
    moodycamel::ConcurrentQueue<TextureLoadRequest> textureRequestQueue;
    moodycamel::ConcurrentQueue<TextureLoadComplete> textureLoadCompleteQueue;

    // Cubemap Loading
    Core::LockFreeHandleAllocator<CubemapLoadSlot, CUBEMAP_JOB_COUNT> cubemapLoadAllocator;
    Core::Array<CubemapLoadSlot, CUBEMAP_JOB_COUNT> cubemapLoadSlots;
    moodycamel::ConcurrentQueue<CubemapLoadRequest> cubemapRequestQueue;
    moodycamel::ConcurrentQueue<CubemapLoadComplete> cubemapLoadCompleteQueue;

    // Sampler loading (processed inline in ThreadMain, no task slot needed)
    moodycamel::ConcurrentQueue<SamplerLoadRequest> samplerRequestQueue;
    moodycamel::ConcurrentQueue<SamplerLoadComplete> samplerLoadCompleteQueue;

    // GPU Uploads
    moodycamel::ConcurrentQueue<GPUDispatchRequest> gpuDispatchQueue;
    Core::LockFreeHandleAllocator<UploadStaging, GPU_DISPATCH_COUNT> uploadStagingAllocator{};
    Core::Array<UploadStaging, GPU_DISPATCH_COUNT> uploadStagings{};

    void OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle);

    void OnPipelineLoadComplete(bool success, PipelineSlotHandle slotHandle);

    void OnModelLoadComplete(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnProceduralModelLoadComplete(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnTextureLoadComplete(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);

    void OnCubemapComplete(bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle);
};
} // AssetLoad
#endif //WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
