//
// Created by William on 2026-01-26.
//

#include "async_asset_load_manager.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_asset.h"
#include "platform/thread_utils.h"
#include "asset-load-jobs/cubemap_load_slot.h"
#include "core/memory/memory_manager.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/model/static_model.h"
#include "engine/resources/sampler/sampler.h"
#include "engine/resources/texture/texture.h"
#include "render/resource_manager.h"
#include "render/pipelines/pipeline_data.h"
#include "render/types/cubemap_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
AsyncAssetLoadManager::AsyncAssetLoadManager(Core::MemoryManager& memoryManager,
                                             Render::VulkanContext* context,
                                             Render::ResourceManager* resourceManager,
                                             VkPipelineCache pipelineCache,
                                             enki::TaskScheduler* scheduler)
    : scheduler(scheduler),
      memoryManager(&memoryManager),
      context(context),
      resourceManager(resourceManager),
      pipelineCache(pipelineCache)
{
    for (uint32_t i = 0; i < AUDIO_JOB_COUNT; ++i) {
        audioLoadSlots[i].Initialize(scheduler, [this](bool success, AudioSlotHandle slotHandle) {
            OnAudioLoadComplete(success, slotHandle);
        });
    }

    for (uint32_t i = 0; i < PIPELINE_JOB_COUNT; ++i) {
        pipelineLoadSlots[i].Initialize(
            scheduler,
            context,
            pipelineCache,
            &memoryManager,
            [this](bool success, PipelineSlotHandle slotHandle) {
                OnPipelineLoadComplete(success, slotHandle);
            });
    }

    for (uint32_t i = 0; i < MODEL_JOB_COUNT; ++i) {
        modelLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                QueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle) {
                OnModelLoadComplete(success, modelSlotHandle, uploadStagingSlotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < PROCEDURAL_MODEL_JOB_COUNT; ++i) {
        proceduralModelLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                QueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle) {
                OnProceduralModelLoadComplete(success, slotHandle, uploadStagingSlotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < TEXTURE_JOB_COUNT; ++i) {
        textureLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                QueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle) {
                OnTextureLoadComplete(success, textureSlotHandle, uploadStagingSlotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < CUBEMAP_JOB_COUNT; ++i) {
        cubemapLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                QueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle) {
                OnCubemapComplete(success, cubemapSlotHandle, uploadStagingSlotHandle);
            }
        );
    }

    for (UploadStaging& uploadStaging : uploadStagings) {
        uploadStaging.Initialize(context, GPU_DISPATCH_STAGING_SIZE);
    }

    thisThread = std::jthread([this] { ThreadMain(); });
    gpuDispatchThread = std::jthread([this] { GPUDispatchThreadMain(); });
}

AsyncAssetLoadManager::~AsyncAssetLoadManager() = default;

void AsyncAssetLoadManager::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AsyncAssetLoadMain");
    Platform::SetThreadName("AsyncAssetLoadMain");

    scheduler->RegisterExternalTaskThread();

    // todo model, texture, audio, and pipeline unload (not all need to be queued)
    while (!bShouldExit.load(std::memory_order_acquire)) {
        {
            ZoneScopedN("Process Audio Requests");
            AudioLoadRequest audioReq{};
            if (audioRequestQueue.try_dequeue(audioReq)) {
                Core::Handle<AudioLoadSlot> slotHandle = audioLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    AudioLoadSlot& slot = audioLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, audioReq.audioEntry);
                }
            }
        } {
            ZoneScopedN("Process Pipeline Requests");
            PipelineLoadRequest pipelineReq{};
            if (pipelineRequestQueue.try_dequeue(pipelineReq)) {
                Core::Handle<PipelineLoadSlot> slotHandle = pipelineLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    PipelineLoadSlot& slot = pipelineLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, pipelineReq.entry);
                }
            }
        } {
            ZoneScopedN("Process Model Requests");
            StaticModelLoadRequest modelReq{};
            if (modelRequestQueue.try_dequeue(modelReq)) {
                Core::Handle<StaticModelLoadSlot> slotHandle = modelLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    Core::Handle<UploadStaging> uploadHandle = uploadStagingAllocator.Add();
                    if (uploadHandle.IsValid()) {
                        StaticModelLoadSlot& slot = modelLoadSlots[slotHandle.index];
                        UploadStaging* uploadStaging = &uploadStagings[uploadHandle.index];

                        slot.Launch(slotHandle, uploadHandle, uploadStaging, modelReq.model);
                    }
                    else {
                        modelRequestQueue.enqueue(modelReq);
                        modelLoadAllocator.Remove(slotHandle);
                    }
                }
                else {
                    modelRequestQueue.enqueue(modelReq);
                }
            }
        } {
            ZoneScopedN("Process Procedural Model Requests");
            StaticModelLoadRequest proceduralReq{};
            if (proceduralModelRequestQueue.try_dequeue(proceduralReq)) {
                Core::Handle<ProceduralModelLoadSlot> slotHandle = proceduralModelLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    Core::Handle<UploadStaging> uploadHandle = uploadStagingAllocator.Add();
                    if (uploadHandle.IsValid()) {
                        ProceduralModelLoadSlot& slot = proceduralModelLoadSlots[slotHandle.index];
                        UploadStaging* uploadStaging = &uploadStagings[uploadHandle.index];

                        slot.Launch(slotHandle, uploadHandle, uploadStaging, proceduralReq.model);
                    }
                    else {
                        proceduralModelRequestQueue.enqueue(proceduralReq);
                        proceduralModelLoadAllocator.Remove(slotHandle);
                    }
                }
                else {
                    proceduralModelRequestQueue.enqueue(proceduralReq);
                }
            }
        } {
            ZoneScopedN("Process Texture Requests");
            TextureLoadRequest textureReq{};
            if (textureRequestQueue.try_dequeue(textureReq)) {
                Core::Handle<TextureLoadSlot> slotHandle = textureLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    Core::Handle<UploadStaging> uploadHandle = uploadStagingAllocator.Add();
                    if (uploadHandle.IsValid()) {
                        TextureLoadSlot& slot = textureLoadSlots[slotHandle.index];
                        UploadStaging* uploadStaging = &uploadStagings[uploadHandle.index];

                        slot.Launch(slotHandle, uploadHandle, uploadStaging, textureReq.texture);
                    }
                    else {
                        textureRequestQueue.enqueue(textureReq);
                        textureLoadAllocator.Remove(slotHandle);
                    }
                }
                else {
                    textureRequestQueue.enqueue(textureReq);
                }
            }
        } {
            ZoneScopedN("Process Cubemap Requests");
            CubemapLoadRequest cubemapReq{};
            if (cubemapRequestQueue.try_dequeue(cubemapReq)) {
                Core::Handle<CubemapLoadSlot> slotHandle = cubemapLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    Core::Handle<UploadStaging> uploadHandle = uploadStagingAllocator.Add();
                    if (uploadHandle.IsValid()) {
                        CubemapLoadSlot& slot = cubemapLoadSlots[slotHandle.index];
                        UploadStaging* uploadStaging = &uploadStagings[uploadHandle.index];

                        slot.Launch(slotHandle, uploadHandle, uploadStaging, cubemapReq.cubemap);
                    }
                    else {
                        cubemapRequestQueue.enqueue(cubemapReq);
                        cubemapLoadAllocator.Remove(slotHandle);
                    }
                }
                else {
                    cubemapRequestQueue.enqueue(cubemapReq);
                }
            }
        } {
            ZoneScopedN("Process Sampler Requests");
            SamplerLoadRequest samplerReq{};
            if (samplerRequestQueue.try_dequeue(samplerReq)) {
                Engine::Sampler* s = samplerReq.sampler;
                s->sampler = Render::Sampler::CreateSampler(context, s->desc.ToVkSamplerCreateInfo());
                bool success = s->sampler.handle != VK_NULL_HANDLE;
                if (success) {
                    resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateSampler(s->bindlessHandle, s->sampler.handle);
                }
                samplerLoadCompleteQueue.enqueue({s, success});
            }
        }

        if (workCounter.load(std::memory_order_acquire) > 0) {
            workCounter.fetch_sub(1);
        }
        else {
            ZoneScopedN("Idle - Waiting for Work");
            std::unique_lock lock(wakeMutex);
            wakeCV.wait(lock, [&] {
                return workCounter.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (workCounter.load(std::memory_order_acquire) > 0) {
                workCounter.fetch_sub(1);
            }
        }
    }
}

void AsyncAssetLoadManager::GPUDispatchThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AsyncGPUDispatcherMain");
    Platform::SetThreadName("AsyncGPUDispatcherMain");

    scheduler->RegisterExternalTaskThread();

    while (!bShouldExit.load(std::memory_order_acquire)) {
        {
            ZoneScopedN("Dispatch GPU Requests");
            constexpr size_t MAX_BATCH = 16;
            GPUDispatchRequest batch[MAX_BATCH];
            VkFence fenceBuf[MAX_BATCH];

            size_t count = gpuDispatchQueue.try_dequeue_bulk(batch, MAX_BATCH);
            if (count > 0) {
                VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submitInfo.commandBufferCount = 1;

                for (size_t i = 0; i < count; ++i) {
                    submitInfo.pCommandBuffers = &batch[i].cmd;
                    VK_CHECK(vkQueueSubmit(this->context->transferQueue, 1, &submitInfo, batch[i].fence));
                    fenceBuf[i] = batch[i].fence;
                }

                VK_CHECK(vkWaitForFences(context->device, static_cast<uint32_t>(count), fenceBuf, VK_TRUE, UINT64_MAX));
                for (size_t i = 0; i < count; ++i) {
                    batch[i].completionSignal->release();
                }
            }
        }

        if (gpuDispatchWorkCounter.load(std::memory_order_acquire) > 0) {
            gpuDispatchWorkCounter.fetch_sub(1);
        }
        else {
            ZoneScopedN("Idle - Waiting for Work");
            std::unique_lock lock(gpuDispatchWakeMutex);
            gpuDispatchWakeCV.wait(lock, [&] {
                return gpuDispatchWorkCounter.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (gpuDispatchWorkCounter.load(std::memory_order_acquire) > 0) {
                gpuDispatchWorkCounter.fetch_sub(1);
            }
        }
    }
}

void AsyncAssetLoadManager::Join()
{
    bShouldExit.store(true, std::memory_order_release);

    gpuDispatchWorkCounter.fetch_add(1);
    gpuDispatchWakeCV.notify_one();
    gpuDispatchThread.join();

    workCounter.fetch_add(1);
    wakeCV.notify_one();
    thisThread.join();
}

void AsyncAssetLoadManager::RequestAudioLoad(Audio::WillAudio* audioEntry)
{
    ZoneScoped;

    if (!audioEntry) {
        LOG_ERROR(Asset, "RequestAudioLoad called with null audioEntry");
        return;
    }

    audioRequestQueue.enqueue({audioEntry});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueAudioComplete(AudioLoadComplete& outResult)
{
    return audioLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestPipelineLoad(Render::PipelineData* pipelineData)
{
    ZoneScoped;

    if (!pipelineData) {
        LOG_ERROR(Asset, "RequestPipelineLoad called with null pipelineData");
        return;
    }

    pipelineRequestQueue.enqueue({pipelineData});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeuePipelineComplete(PipelineLoadComplete& outResult)
{
    return pipelineLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestModelLoad(Engine::StaticModel* model)
{
    ZoneScoped;

    if (!model) {
        LOG_ERROR(Asset, "RequestModelLoad called with null model");
        return;
    }

    modelRequestQueue.enqueue({model});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueModelComplete(StaticModelLoadComplete& outResult)
{
    return modelLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestProceduralModelLoad(Engine::StaticModel* model)
{
    ZoneScoped;

    if (!model) {
        LOG_ERROR(Asset, "RequestProceduralModelLoad called with null model");
        return;
    }

    proceduralModelRequestQueue.enqueue({model});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueProceduralModelComplete(StaticModelLoadComplete& outResult)
{
    return proceduralModelLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestTextureLoad(Engine::Texture* texture)
{
    ZoneScoped;

    if (!texture) {
        LOG_ERROR(Asset, "RequestTextureLoad called with null texture");
        return;
    }

    textureRequestQueue.enqueue({texture});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueTextureComplete(TextureLoadComplete& outResult)
{
    return textureLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestCubemapLoad(Render::Cubemap* cubemap)
{
    ZoneScoped;
    if (!cubemap) {
        LOG_ERROR(Asset, "RequestCubemapLoad called with null cubemap");
        return;
    }
    cubemapRequestQueue.enqueue({cubemap});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueCubemapComplete(CubemapLoadComplete& outResult)
{
    return cubemapLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestSamplerLoad(Engine::Sampler* sampler)
{
    ZoneScoped;

    if (!sampler) {
        LOG_ERROR(Asset, "RequestSamplerLoad called with null sampler");
        return;
    }

    samplerRequestQueue.enqueue({sampler});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueSamplerComplete(SamplerLoadComplete& outResult)
{
    return samplerLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::QueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)
{
    gpuDispatchQueue.enqueue({cmd, fence, completionSignal});
    gpuDispatchWorkCounter.fetch_add(1);
    gpuDispatchWakeCV.notify_one();
}

void AsyncAssetLoadManager::OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle)
{
    ZoneScoped;

    if (!audioLoadAllocator.IsValid(slotHandle)) {
        LOG_ERROR(Asset, "OnAudioLoadComplete called with invalid slot handle");
        return;
    }

    AudioLoadSlot& slot = audioLoadSlots[slotHandle.index];
    audioLoadCompleteQueue.enqueue({slot.audioEntry, success});

    if (success) {
        LOG_TRACE(Asset, "Finished loading audio file: {}", slot.audioEntry->source.c_str());
    }
    else {
        LOG_ERROR(Asset, "Failed to load audio file: {}", slot.audioEntry->source.c_str());
    }

    slot.Clear();
    bool removed = audioLoadAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnPipelineLoadComplete(bool success, PipelineSlotHandle slotHandle)
{
    ZoneScoped;

    if (!pipelineLoadAllocator.IsValid(slotHandle)) {
        LOG_ERROR(Asset, "OnPipelineLoadComplete called with invalid slot handle");
        return;
    }

    PipelineLoadSlot& slot = pipelineLoadSlots[slotHandle.index];
    pipelineLoadCompleteQueue.enqueue({slot.pipelineData, success});

    if (!success) {
        LOG_ERROR(Asset, "Failed to load pipeline");
    }

    slot.Clear();
    bool removed = pipelineLoadAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnModelLoadComplete(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)
{
    ZoneScoped;

    if (!modelLoadAllocator.IsValid(modelSlotHandle)) {
        LOG_ERROR(Asset, "OnModelLoadComplete called with invalid slot handle");
        return;
    }

    StaticModelLoadSlot& slot = modelLoadSlots[modelSlotHandle.index];
    modelLoadCompleteQueue.enqueue({slot.outputModel, success});

    if (!success) {
        LOG_ERROR(Asset, "Failed to load model: {}", slot.outputModel->name.c_str());
    }

    slot.Clear();
    modelLoadAllocator.Remove(modelSlotHandle);

    if (uploadStagingAllocator.IsValid(uploadStagingSlotHandle)) {
        uploadStagingAllocator.Remove(uploadStagingSlotHandle);
    }

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnProceduralModelLoadComplete(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)
{
    ZoneScoped;

    if (!proceduralModelLoadAllocator.IsValid(slotHandle)) {
        LOG_ERROR(Asset, "OnProceduralModelLoadComplete called with invalid slot handle");
        return;
    }

    ProceduralModelLoadSlot& slot = proceduralModelLoadSlots[slotHandle.index];
    proceduralModelLoadCompleteQueue.enqueue({slot.outputModel, success});

    if (!success) {
        LOG_ERROR(Asset, "Failed to generate procedural model: {}", slot.outputModel->name.c_str());
    }

    slot.Clear();
    proceduralModelLoadAllocator.Remove(slotHandle);

    if (uploadStagingAllocator.IsValid(uploadStagingSlotHandle)) {
        uploadStagingAllocator.Remove(uploadStagingSlotHandle);
    }

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnTextureLoadComplete(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)
{
    ZoneScoped;

    if (!textureLoadAllocator.IsValid(textureSlotHandle)) {
        LOG_ERROR(Asset, "OnTextureLoadComplete called with invalid slot handle");
        return;
    }

    TextureLoadSlot& slot = textureLoadSlots[textureSlotHandle.index];
    textureLoadCompleteQueue.enqueue({slot.outputTexture, success});

    if (success) {
        LOG_TRACE(Asset, "Finished loading texture: {}", slot.outputTexture->source.c_str());
    }
    else {
        LOG_ERROR(Asset, "Failed to load texture: {}", slot.outputTexture->source.c_str());
    }

    slot.Clear();
    textureLoadAllocator.Remove(textureSlotHandle);

    if (uploadStagingAllocator.IsValid(uploadStagingSlotHandle)) {
        uploadStagingAllocator.Remove(uploadStagingSlotHandle);
    }

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnCubemapComplete(bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)
{
    ZoneScoped;

    if (!cubemapLoadAllocator.IsValid(cubemapSlotHandle)) {
        LOG_ERROR(Asset, "OnCubemapComplete called with invalid slot handle");
        return;
    }

    CubemapLoadSlot& slot = cubemapLoadSlots[cubemapSlotHandle.index];
    cubemapLoadCompleteQueue.enqueue({slot.outputCubemap, success});

    if (success) {
        LOG_TRACE(Asset, "Finished loading cubemap: {}", slot.outputCubemap->source.c_str());
    }
    else {
        LOG_ERROR(Asset, "Failed to load cubemap: {}", slot.outputCubemap->source.c_str());
    }

    slot.Clear();
    cubemapLoadAllocator.Remove(cubemapSlotHandle);

    if (uploadStagingAllocator.IsValid(uploadStagingSlotHandle)) {
        uploadStagingAllocator.Remove(uploadStagingSlotHandle);
    }

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}
} // AssetLoad
