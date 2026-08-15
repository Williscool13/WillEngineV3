//
// Created by William on 2026-01-26.
//

#include "async_asset_load_manager.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_asset.h"
#include "platform/thread_utils.h"
#include "asset-load-jobs/cubemap_load_slot.h"
#include "asset-load-jobs/text3d_geometry.h"
#include "core/memory/memory_manager.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/font/font.h"
#include "engine/resources/model/static_model.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "engine/resources/sampler/sampler.h"
#include "engine/resources/texture/texture.h"
#include "par/par_shapes_ext.h"
#include "render/gpu_dispatcher.h"
#include "render/resource_manager.h"
#include "render/pipelines/pipeline_data.h"
#include "render/types/cubemap_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
static uint64_t Mip0ByteSize(const Engine::Texture* texture)
{
    const uint32_t width = texture->width;
    const uint32_t height = texture->height;
    switch (texture->format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return static_cast<uint64_t>((width + 3) / 4) * ((height + 3) / 4) * 8;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return static_cast<uint64_t>((width + 3) / 4) * ((height + 3) / 4) * 16;
        case VK_FORMAT_UNDEFINED:
            // Disk textures don't know their format until the image blob is parsed; full chain is ~4/3 of mip0
            return texture->uncompressedSize * 3 / 4;
        default:
            return static_cast<uint64_t>(width) * height * Render::VkHelpers::GetBytesPerPixel(texture->format);
    }
}

AsyncAssetLoadManager::AsyncAssetLoadManager(Core::MemoryManager& memoryManager,
                                             Render::VulkanContext* context,
                                             Render::ResourceManager* resourceManager,
                                             Render::PipelineManager* pipelineManager,
                                             VkPipelineCache pipelineCache,
                                             Render::GPUDispatcher* gpuDispatcher,
                                             enki::TaskScheduler* scheduler)
    : scheduler(scheduler),
      memoryManager(&memoryManager),
      context(context),
      resourceManager(resourceManager),
      pipelineManager(pipelineManager),
      pipelineCache(pipelineCache),
      gpuDispatcher(gpuDispatcher)
{
    par_shapes_set_allocator(&memoryManager.AssetsScratch());
    SetEarcutAllocator(&memoryManager.AssetsScratch());

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

    stagingDepot.Initialize(context, UPLOAD_STAGING_BUDGET_DEFAULT);

    for (uint32_t i = 0; i < MODEL_JOB_COUNT; ++i) {
        modelLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore signalSemaphore) {
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Transfer, cmd, fence, completionSignal, signalSemaphore);
            },
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore waitSemaphore) {
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Graphics, cmd, fence, completionSignal, VK_NULL_HANDLE, waitSemaphore);
            },
            [this](bool success, ModelSlotHandle modelSlotHandle) {
                OnModelLoadComplete(success, modelSlotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < PROCEDURAL_MODEL_JOB_COUNT; ++i) {
        proceduralModelLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore signalSemaphore) {
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Transfer, cmd, fence, completionSignal, signalSemaphore);
            },
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore waitSemaphore) {
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Graphics, cmd, fence, completionSignal, VK_NULL_HANDLE, waitSemaphore);
            },
            [this](bool success, ProceduralModelSlotHandle slotHandle) {
                OnProceduralModelLoadComplete(success, slotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < PHYSICS_COLLIDER_JOB_COUNT; ++i) {
        physicsColliderLoadSlots[i].Initialize(
            scheduler,
            &memoryManager,
            [this](bool success, PhysicsColliderSlotHandle slotHandle) {
                OnPhysicsColliderLoadComplete(success, slotHandle);
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
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Transfer, cmd, fence, completionSignal);
            },
            [this](bool success, TextureSlotHandle textureSlotHandle) {
                OnTextureLoadComplete(success, textureSlotHandle);
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
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Transfer, cmd, fence, completionSignal);
            },
            [this](bool success, CubemapSlotHandle cubemapSlotHandle) {
                OnCubemapComplete(success, cubemapSlotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < FONT_CURVE_JOB_COUNT; ++i) {
        fontCurveLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Transfer, cmd, fence, completionSignal);
            },
            [this](bool success, FontCurveSlotHandle slotHandle) {
                OnFontCurveLoadComplete(success, slotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < PROCEDURAL_TEXTURE_JOB_COUNT; ++i) {
        proceduralTextureLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            pipelineManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                this->gpuDispatcher->Enqueue(Render::DispatchChannel::Compute, cmd, fence, completionSignal);
            },
            [this](bool success, ProceduralTextureSlotHandle slotHandle) {
                OnProceduralTextureLoadComplete(success, slotHandle);
            }
        );
    }

    thisThread = std::jthread([this] { ThreadMain(); });
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
                else {
                    audioRequestQueue.enqueue(audioReq);
                }
            }
        }
        //
        {
            ZoneScopedN("Process Pipeline Requests");
            PipelineLoadRequest pipelineReq{};
            if (pipelineRequestQueue.try_dequeue(pipelineReq)) {
                Core::Handle<PipelineLoadSlot> slotHandle = pipelineLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    PipelineLoadSlot& slot = pipelineLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, pipelineReq.entry);
                }
                else {
                    pipelineRequestQueue.enqueue(pipelineReq);
                }
            }
        }
        //
        {
            ZoneScopedN("Process Model Requests");
            StaticModelLoadRequest modelReq{};
            if (modelRequestQueue.try_dequeue(modelReq)) {
                Core::Handle<StaticModelLoadSlot> slotHandle = modelLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    UploadStaging* uploadStaging = stagingDepot.CheckOut(modelReq.model->uncompressedBodySize);
                    if (uploadStaging) {
                        StaticModelLoadSlot& slot = modelLoadSlots[slotHandle.index];
                        slot.Launch(slotHandle, uploadStaging, modelReq.model);
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
        }
        //
        {
            ZoneScopedN("Process Procedural Model Requests");
            StaticModelLoadRequest proceduralReq{};
            if (proceduralModelRequestQueue.try_dequeue(proceduralReq)) {
                Core::Handle<ProceduralModelLoadSlot> slotHandle = proceduralModelLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    UploadStaging* uploadStaging = stagingDepot.CheckOut(proceduralReq.model->uncompressedBodySize);
                    if (uploadStaging) {
                        ProceduralModelLoadSlot& slot = proceduralModelLoadSlots[slotHandle.index];
                        slot.Launch(slotHandle, uploadStaging, proceduralReq.model);
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
        }
        //
        {
            ZoneScopedN("Process Physics Collider Requests");
            PhysicsColliderLoadRequest colliderReq{};
            if (physicsColliderRequestQueue.try_dequeue(colliderReq)) {
                Core::Handle<PhysicsColliderLoadSlot> slotHandle = physicsColliderLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    PhysicsColliderLoadSlot& slot = physicsColliderLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, colliderReq.collider);
                }
                else {
                    physicsColliderRequestQueue.enqueue(colliderReq);
                }
            }
        }
        //
        {
            ZoneScopedN("Process Texture Requests");
            TextureLoadRequest textureReq{};
            if (textureRequestQueue.try_dequeue(textureReq)) {
                Core::Handle<TextureLoadSlot> slotHandle = textureLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    UploadStaging* uploadStaging = stagingDepot.CheckOut(Mip0ByteSize(textureReq.texture) / UPLOAD_STAGING_MIP0_DIVISOR);
                    if (uploadStaging) {
                        TextureLoadSlot& slot = textureLoadSlots[slotHandle.index];
                        slot.Launch(slotHandle, uploadStaging, textureReq.texture);
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
        }
        //
        {
            ZoneScopedN("Process Cubemap Requests");
            CubemapLoadRequest cubemapReq{};
            if (cubemapRequestQueue.try_dequeue(cubemapReq)) {
                Core::Handle<CubemapLoadSlot> slotHandle = cubemapLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    // 6 faces of a full chain sum to ~8x one face's mip0
                    UploadStaging* uploadStaging = stagingDepot.CheckOut((cubemapReq.cubemap->uncompressedSize / 8) / UPLOAD_STAGING_MIP0_DIVISOR);
                    if (uploadStaging) {
                        CubemapLoadSlot& slot = cubemapLoadSlots[slotHandle.index];
                        slot.Launch(slotHandle, uploadStaging, cubemapReq.cubemap);
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
        }
        //
        {
            ZoneScopedN("Process Font Curve Requests");
            FontCurveLoadRequest fontCurveReq{};
            if (fontCurveRequestQueue.try_dequeue(fontCurveReq)) {
                Core::Handle<FontCurveLoadSlot> slotHandle = fontCurveLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    UploadStaging* uploadStaging = stagingDepot.CheckOut(std::max(fontCurveReq.font->header.slugUncompressedSize, fontCurveReq.font->header.sdfUncompressedSize));
                    if (uploadStaging) {
                        FontCurveLoadSlot& slot = fontCurveLoadSlots[slotHandle.index];
                        slot.Launch(slotHandle, uploadStaging, fontCurveReq.font);
                    }
                    else {
                        fontCurveRequestQueue.enqueue(fontCurveReq);
                        fontCurveLoadAllocator.Remove(slotHandle);
                    }
                }
                else {
                    fontCurveRequestQueue.enqueue(fontCurveReq);
                }
            }
        }
        //
        {
            ZoneScopedN("Process Procedural Texture Requests");
            ProceduralTextureLoadRequest proceduralTexReq{};
            if (proceduralTextureRequestQueue.try_dequeue(proceduralTexReq)) {
                Core::Handle<ProceduralTextureLoadSlot> slotHandle = proceduralTextureLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    ProceduralTextureLoadSlot& slot = proceduralTextureLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, proceduralTexReq.texture, proceduralTexReq.pipelineId);
                }
                else {
                    proceduralTextureRequestQueue.enqueue(proceduralTexReq);
                }
            }
        }
        //
        {
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
            // Timed wait: requeued requests (staging exhausted / slots full) don't bump workCounter, so guarantee a periodic re-poll
            wakeCV.wait_for(lock, std::chrono::milliseconds(10), [&] {
                return workCounter.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (workCounter.load(std::memory_order_acquire) > 0) {
                workCounter.fetch_sub(1);
            }
        }
    }
}

void AsyncAssetLoadManager::BeginShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::Join()
{
    BeginShutdown();
    thisThread.join();
}

void AsyncAssetLoadManager::RequestAudioLoad(Audio::WillAudio* audioEntry)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
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

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
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

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
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

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
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

void AsyncAssetLoadManager::RequestPhysicsColliderLoad(Engine::PhysicsColliderAsset* collider)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
    if (!collider) {
        LOG_ERROR(Asset, "RequestPhysicsColliderLoad called with null collider");
        return;
    }

    physicsColliderRequestQueue.enqueue({collider});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeuePhysicsColliderComplete(PhysicsColliderLoadComplete& outResult)
{
    return physicsColliderLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestTextureLoad(Engine::Texture* texture)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
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
    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
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

void AsyncAssetLoadManager::RequestFontCurveLoad(Engine::Font* font)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
    if (!font) {
        LOG_ERROR(Asset, "RequestFontCurveLoad called with null font");
        return;
    }

    fontCurveRequestQueue.enqueue({font});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueFontCurveComplete(FontCurveLoadComplete& outResult)
{
    return fontCurveLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestSamplerLoad(Engine::Sampler* sampler)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
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

void AsyncAssetLoadManager::RequestProceduralTextureLoad(Engine::Texture* texture, StringID pipelineId)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
    if (!texture) {
        LOG_ERROR(Asset, "RequestProceduralTextureLoad called with null texture");
        return;
    }

    proceduralTextureRequestQueue.enqueue({texture, pipelineId});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueProceduralTextureComplete(ProceduralTextureLoadComplete& outResult)
{
    return proceduralTextureCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::OnProceduralTextureLoadComplete(bool success, ProceduralTextureSlotHandle slotHandle)
{
    ZoneScoped;

    if (!proceduralTextureLoadAllocator.IsValid(slotHandle)) {
        LOG_ERROR(Asset, "OnProceduralTextureLoadComplete called with invalid slot handle");
        return;
    }

    ProceduralTextureLoadSlot& slot = proceduralTextureLoadSlots[slotHandle.index];
    proceduralTextureCompleteQueue.enqueue({slot.outputTexture, success});

    if (!success) {
        LOG_ERROR(Asset, "Failed to generate procedural texture: {:x}", slot.outputTexture->textureId.id);
    }

    slot.Clear();
    proceduralTextureLoadAllocator.Remove(slotHandle);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
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

void AsyncAssetLoadManager::OnModelLoadComplete(bool success, ModelSlotHandle modelSlotHandle)
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

    if (slot.uploadStaging) {
        stagingDepot.Return(slot.uploadStaging);
    }
    slot.Clear();
    modelLoadAllocator.Remove(modelSlotHandle);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnProceduralModelLoadComplete(bool success, ProceduralModelSlotHandle slotHandle)
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

    if (slot.uploadStaging) {
        stagingDepot.Return(slot.uploadStaging);
    }
    slot.Clear();
    proceduralModelLoadAllocator.Remove(slotHandle);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnPhysicsColliderLoadComplete(bool success, PhysicsColliderSlotHandle slotHandle)
{
    ZoneScoped;

    if (!physicsColliderLoadAllocator.IsValid(slotHandle)) {
        LOG_ERROR(Asset, "OnPhysicsColliderLoadComplete called with invalid slot handle");
        return;
    }

    PhysicsColliderLoadSlot& slot = physicsColliderLoadSlots[slotHandle.index];
    physicsColliderLoadCompleteQueue.enqueue({slot.collider, success});

    if (!success && slot.collider) {
        LOG_ERROR(Asset, "Failed to generate physics collider: {}", slot.collider->name.c_str());
    }

    slot.Clear();
    physicsColliderLoadAllocator.Remove(slotHandle);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnFontCurveLoadComplete(bool success, FontCurveSlotHandle slotHandle)
{
    ZoneScoped;

    if (!fontCurveLoadAllocator.IsValid(slotHandle)) {
        LOG_ERROR(Asset, "OnFontCurveLoadComplete called with invalid slot handle");
        return;
    }

    FontCurveLoadSlot& slot = fontCurveLoadSlots[slotHandle.index];
    fontCurveLoadCompleteQueue.enqueue({slot.outputFont, success});

    if (!success) {
        LOG_ERROR(Asset, "Failed to load font curves: {}", slot.outputFont->name.c_str());
    }

    if (slot.uploadStaging) {
        stagingDepot.Return(slot.uploadStaging);
    }
    slot.Clear();
    fontCurveLoadAllocator.Remove(slotHandle);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnTextureLoadComplete(bool success, TextureSlotHandle textureSlotHandle)
{
    ZoneScoped;

    if (!textureLoadAllocator.IsValid(textureSlotHandle)) {
        LOG_ERROR(Asset, "OnTextureLoadComplete called with invalid slot handle");
        return;
    }

    TextureLoadSlot& slot = textureLoadSlots[textureSlotHandle.index];
    textureLoadCompleteQueue.enqueue({slot.outputTexture, success});

    if (!success) {
        LOG_ERROR(Asset, "Failed to load texture: {}", slot.outputTexture->source.c_str());
    }

    if (slot.uploadStaging) {
        stagingDepot.Return(slot.uploadStaging);
    }
    slot.Clear();
    textureLoadAllocator.Remove(textureSlotHandle);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnCubemapComplete(bool success, CubemapSlotHandle cubemapSlotHandle)
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

    if (slot.uploadStaging) {
        stagingDepot.Return(slot.uploadStaging);
    }
    slot.Clear();
    cubemapLoadAllocator.Remove(cubemapSlotHandle);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}
} // AssetLoad
