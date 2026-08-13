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
#include "asset-load-jobs/physics_collider_load_slot.h"
#include "asset-load-jobs/texture_load_slot.h"
#include "asset-load-jobs/cubemap_load_slot.h"
#include "asset-load-jobs/procedural_texture_load_slot.h"
#include "core/containers/array.h"
#include "core/memory/lock_free_handle_allocator.h"
#include "engine/resources/sampler/sampler.h"

namespace Render
{
class GPUDispatcher;
}

namespace AssetLoad
{
class AsyncAssetLoadManager
{
public:
    AsyncAssetLoadManager(Core::MemoryManager& memoryManager,
                          Render::VulkanContext* context,
                          Render::ResourceManager* resourceManager,
                          Render::PipelineManager* pipelineManager,
                          VkPipelineCache pipelineCache,
                          Render::GPUDispatcher* gpuDispatcher,
                          enki::TaskScheduler* scheduler);

    ~AsyncAssetLoadManager();

    AsyncAssetLoadManager(const AsyncAssetLoadManager&) = delete;

    AsyncAssetLoadManager& operator=(const AsyncAssetLoadManager&) = delete;

    AsyncAssetLoadManager(AsyncAssetLoadManager&&) = delete;

    AsyncAssetLoadManager& operator=(AsyncAssetLoadManager&&) = delete;

    void ThreadMain();

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

    // Physics collider loading (CPU-only)
    void RequestPhysicsColliderLoad(Engine::PhysicsColliderAsset* collider);

    bool TryDequeuePhysicsColliderComplete(PhysicsColliderLoadComplete& outResult);


    // Texture loading
    void RequestTextureLoad(Engine::Texture* texture);

    bool TryDequeueTextureComplete(TextureLoadComplete& outResult);

    // Cubemap loading
    void RequestCubemapLoad(Render::Cubemap* cubemap);

    bool TryDequeueCubemapComplete(CubemapLoadComplete& outResult);

    // Sampler loading
    void RequestSamplerLoad(Engine::Sampler* sampler);

    bool TryDequeueSamplerComplete(SamplerLoadComplete& outResult);

    // Procedural texture loading
    void RequestProceduralTextureLoad(Engine::Texture* texture, StringID pipelineId);

    bool TryDequeueProceduralTextureComplete(ProceduralTextureLoadComplete& outResult);


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

    [[nodiscard]] uint32_t GetActiveProceduralModelLoadCount() const
    {
        return proceduralModelLoadAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActivePhysicsColliderLoadCount() const
    {
        return physicsColliderLoadAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActiveCubemapLoadCount() const
    {
        return cubemapLoadAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActiveProceduralTextureLoadCount() const
    {
        return proceduralTextureLoadAllocator.GetCount();
    }

    /** Slack knob: raise for loading screens, lower for gameplay; floored at UPLOAD_STAGING_MAX_SIZE. Wakes the load thread so requeued requests re-attempt under the new budget. */
    void SetUploadStagingBudget(uint64_t budgetBytes)
    {
        stagingDepot.SetBudgetBytes(budgetBytes);
        workCounter.fetch_add(1);
        wakeCV.notify_one();
    }

private:
    enki::TaskScheduler* scheduler{};
    Core::MemoryManager* memoryManager{};
    Render::VulkanContext* context;
    Render::ResourceManager* resourceManager;
    Render::PipelineManager* pipelineManager;
    VkPipelineCache pipelineCache;
    Render::GPUDispatcher* gpuDispatcher{};
    std::atomic<bool> bShouldExit{false};

    std::jthread thisThread;
    std::atomic<uint32_t> workCounter{0};
    std::mutex wakeMutex;
    std::condition_variable wakeCV;

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

    // Physics Collider Loading (CPU-only)
    Core::LockFreeHandleAllocator<PhysicsColliderLoadSlot, PHYSICS_COLLIDER_JOB_COUNT> physicsColliderLoadAllocator;
    Core::Array<PhysicsColliderLoadSlot, PHYSICS_COLLIDER_JOB_COUNT> physicsColliderLoadSlots;
    moodycamel::ConcurrentQueue<PhysicsColliderLoadRequest> physicsColliderRequestQueue;
    moodycamel::ConcurrentQueue<PhysicsColliderLoadComplete> physicsColliderLoadCompleteQueue;

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

    // Procedural Texture Loading
    Core::LockFreeHandleAllocator<ProceduralTextureLoadSlot, PROCEDURAL_TEXTURE_JOB_COUNT> proceduralTextureLoadAllocator;
    Core::Array<ProceduralTextureLoadSlot, PROCEDURAL_TEXTURE_JOB_COUNT> proceduralTextureLoadSlots;
    moodycamel::ConcurrentQueue<ProceduralTextureLoadRequest> proceduralTextureRequestQueue;
    moodycamel::ConcurrentQueue<ProceduralTextureLoadComplete> proceduralTextureCompleteQueue;

    UploadStagingDepot stagingDepot{};

    void OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle);

    void OnPipelineLoadComplete(bool success, PipelineSlotHandle slotHandle);

    void OnModelLoadComplete(bool success, ModelSlotHandle modelSlotHandle);

    void OnProceduralModelLoadComplete(bool success, ProceduralModelSlotHandle slotHandle);

    void OnPhysicsColliderLoadComplete(bool success, PhysicsColliderSlotHandle slotHandle);

    void OnTextureLoadComplete(bool success, TextureSlotHandle textureSlotHandle);

    void OnCubemapComplete(bool success, CubemapSlotHandle cubemapSlotHandle);

    void OnProceduralTextureLoadComplete(bool success, ProceduralTextureSlotHandle slotHandle);
};
} // AssetLoad
#endif //WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
