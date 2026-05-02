//
// Created by William on 2025-12-15.
//

#include "asset_generator.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "miscellaneous_asset_generate.h"
#include "asset-load/async_asset_load_manager.h"
#include "engine/resources/environment_map/environment_map_format.h"
#include "engine/resources/model/model_format.h"
#include "engine/resources/texture/texture_format.h"
#include "engine/include/engine_context.h"
#include "platform/thread_utils.h"
#include "render/render_thread.h"


namespace Editor
{
AssetGenerator::AssetGenerator(Core::MemoryManager& memoryManager,
                               Engine::EngineContext* ctx,
                               Render::VulkanContext* vulkanContext,
                               Render::RenderThread* renderThread,
                               AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager,
                               enki::TaskScheduler* scheduler)
    : memoryManager(&memoryManager),
      ctx(ctx),
      vk(vulkanContext),
      renderThread(renderThread),
      asyncAssetLoadManager(asyncAssetLoadManager),
      scheduler(scheduler)
{
    for (int32_t i = 0; i < MODEL_GENERATION_JOB_COUNT; ++i) {
        modelGenerateTasks[i].Initialize(
            &memoryManager,
            scheduler,
            this,
            &modelGenerationProgress[i],
            [this](bool success, ModelGenerateSlotHandle slotHandle) {
                OnModelGenerateComplete(success, slotHandle);
            }
        );
    }

    for (int32_t i = 0; i < TEXTURE_GENERATION_JOB_COUNT; ++i) {
        textureGenerateTasks[i].Initialize(
            scheduler,
            vulkanContext,
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                GraphicsQueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, TextureGenerateSlotHandle slotHandle) {
                OnTextureGenerateComplete(success, slotHandle);
            }
        );
    }
    for (int32_t i = 0; i < ENVIRONMENT_MAP_GENERATION_JOB_COUNT; ++i) {
        environmentMapeGenerateTasks[i].Initialize(
            scheduler,
            vulkanContext,
            renderThread->GetPipelineManager(),
            renderThread->GetResourceManager(),
            &memoryManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                GraphicsQueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, EnvironmentMapGenerateSlotHandle slotHandle) {
                OnEnvironmentGenerateComplete(success, slotHandle);
            }
        );
    }

    thisThread = std::jthread([this] { ThreadMain(); });
}

AssetGenerator::~AssetGenerator() = default;

void AssetGenerator::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AssetGeneratorMain");
    Platform::SetThreadName("AssetGeneratorMain");

    scheduler->RegisterExternalTaskThread();

    while (!bShouldExit.load(std::memory_order_acquire)) {
        {
            ZoneScopedN("Process Model Generation Requests");
            ModelGenerateRequest req{};
            if (modelGenerateRequestQueue.try_dequeue(req)) {
                Core::Handle<StaticModelGenerateSlot> slotHandle = modelGenerateAllocator.Add();
                if (slotHandle.IsValid()) {
                    StaticModelGenerateSlot& task = modelGenerateTasks[slotHandle.index];
                    task.Launch(slotHandle, req.gltfPath, req.outputPath, req.modelId);
                }
                else {
                    modelGenerateRequestQueue.enqueue(req);
                }
            }
        }

        //
        {
            ZoneScopedN("Process Texture Generation Requests");
            TextureGenerateRequest req{};
            if (textureGenerateRequestQueue.try_dequeue(req)) {
                Core::Handle<TextureGenerateSlot> slotHandle = textureGenerateAllocator.Add();
                if (slotHandle.IsValid()) {
                    TextureGenerateSlot& task = textureGenerateTasks[slotHandle.index];
                    if (req.sourcePixels.IsAllocated()) {
                        task.LaunchFromMemory(slotHandle, std::move(req.sourcePixels), req.sourceWidth, req.sourceHeight, req.sourceBytesPerPixel, req.outputPath, req.textureId, req.mipmapped,
                                              req.targetFormat);
                    }
                    else {
                        task.Launch(slotHandle, req.imagePath, req.outputPath, req.textureId, req.mipmapped, req.targetFormat);
                    }
                }
                else {
                    textureGenerateRequestQueue.enqueue(std::move(req));
                }
            }
        }

        //
        {
            ZoneScopedN("Process Environment Map Generation Requests")
            EnvironmentMapGenerateRequest req{};
            if (environmentMapGenerateRequestQueue.try_dequeue(req)) {
                Core::Handle<EnvironmentMapGenerateSlot> slotHandle = environmentMapGenerateAllocator.Add();
                if (slotHandle.IsValid()) {
                    EnvironmentMapGenerateSlot& task = environmentMapeGenerateTasks[slotHandle.index];
                    task.Launch(slotHandle, req.imagePath, req.outputPath, req.environmentMapId);
                }
                else {
                    environmentMapGenerateRequestQueue.enqueue(req);
                }
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

void AssetGenerator::Join()
{
    bShouldExit.store(true, std::memory_order_release);
    workCounter.fetch_add(1);
    wakeCV.notify_one();
    thisThread.join();
}

void AssetGenerator::TransferQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const
{
    asyncAssetLoadManager->QueueGPUDispatch(cmd, fence, completionSignal);
}

void AssetGenerator::GraphicsQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const
{
    renderThread->editorGPUDispatchQueue.enqueue({cmd, fence, completionSignal});
}

void AssetGenerator::RequestModelGenerate(const Core::Path& gltfPath, const Core::Path& outputPath)
{
    ZoneScoped;

    uint64_t modelId = modelIdRng();
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWStaticModelHeader(outputPath)) {
            modelId = header->modelId;
        }
    }
    modelGenerateRequestQueue.enqueue({gltfPath, outputPath, modelId});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

Engine::TextureID AssetGenerator::RequestTextureGenerateFromFile(const Core::Path& imagePath, const Core::Path& outputPath, bool mipmapped, DXGI_FORMAT targetFormat)
{
    ZoneScoped;
    Engine::TextureID id{textureIdRng()};
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWTextureHeader(outputPath)) {
            id = Engine::TextureID{header->textureId};
        }
    }
    textureGenerateRequestQueue.enqueue({outputPath, id, mipmapped, targetFormat, imagePath});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
    return id;
}

Engine::TextureID AssetGenerator::RequestTextureGenerateFromMemory(Core::HeapArray<uint8_t> pixels, uint32_t w, uint32_t h, uint32_t bytesPerPixel, const Core::Path& outputPath,
                                                                   bool mipmapped, DXGI_FORMAT targetFormat)
{
    ZoneScoped;
    Engine::TextureID id{textureIdRng()};
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWTextureHeader(outputPath)) {
            id = Engine::TextureID{header->textureId};
        }
    }
    TextureGenerateRequest req{};
    req.outputPath = outputPath;
    req.textureId = id;
    req.mipmapped = mipmapped;
    req.targetFormat = targetFormat;
    req.sourcePixels = std::move(pixels);
    req.sourceWidth = w;
    req.sourceHeight = h;
    req.sourceBytesPerPixel = bytesPerPixel;
    textureGenerateRequestQueue.enqueue(std::move(req));
    workCounter.fetch_add(1);
    wakeCV.notify_one();
    return id;
}

void AssetGenerator::RequestEnvironmentMapGenerate(const Core::Path& hdriPath, const Core::Path& outputPath)
{
    ZoneScoped;

    Engine::EnvironmentMapID id{environmentMapIdRng()};
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWEnvMapHeader(outputPath)) {
            id = Engine::EnvironmentMapID{header->environmentMapId};
        }
    }
    environmentMapGenerateRequestQueue.enqueue({hdriPath, outputPath, id});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}


bool AssetGenerator::TryDequeueModelGenerateComplete(ModelGenerateComplete& outResult)
{
    return modelGenerateCompleteQueue.try_dequeue(outResult);
}

bool AssetGenerator::TryDequeueTextureGenerateComplete(TextureGenerateComplete& outResult)
{
    return textureGenerateCompleteQueue.try_dequeue(outResult);
}

bool AssetGenerator::TryDequeueCubemapGenerateComplete(EnvironmentMapGenerateComplete& outResult)
{
    return environmentMapGenerateCompleteQueue.try_dequeue(outResult);
}

void AssetGenerator::GenerateBRDFLUT(const Core::Path& outputFile)
{
    CreateBRDFLookupTable(memoryManager, outputFile, Engine::TextureID(textureIdRng()), vk, renderThread->GetResourceManager(), renderThread->GetPipelineManager(),
                          [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                              GraphicsQueueGPUDispatch(cmd, fence, completionSignal);
                          });
}

void AssetGenerator::GenerateSMAATextures(const Core::Path& parentDirectory)
{
    CreateSMAATextures(memoryManager,
                       parentDirectory / "smaa_area.wtexture",
                       parentDirectory / "smaa_search.wtexture",
                       Engine::TextureID(textureIdRng()),
                       Engine::TextureID(textureIdRng()));
}

void AssetGenerator::OnModelGenerateComplete(bool success, ModelGenerateSlotHandle slotHandle)
{
    ZoneScoped;

    if (!modelGenerateAllocator.IsValid(slotHandle)) {
        SPDLOG_ERROR("OnModelGenerateComplete called with invalid slot handle");
        return;
    }

    StaticModelGenerateSlot& task = modelGenerateTasks[slotHandle.index];
    modelGenerateCompleteQueue.enqueue({task.outputPath, success});

    if (success) {
        SPDLOG_INFO("Successfully generated model: {}", task.outputPath.c_str());
    }
    else {
        SPDLOG_ERROR("Failed to generate model: {}", task.outputPath.c_str());
    }

    task.Clear();
    bool removed = modelGenerateAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    ctx->bShouldRescanResources.store(true, std::memory_order_release);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AssetGenerator::OnTextureGenerateComplete(bool success, TextureGenerateSlotHandle slotHandle)
{
    ZoneScoped;

    if (!textureGenerateAllocator.IsValid(slotHandle)) {
        SPDLOG_ERROR("OnTextureGenerateComplete called with invalid slot handle");
        return;
    }

    TextureGenerateSlot& task = textureGenerateTasks[slotHandle.index];
    textureGenerateCompleteQueue.enqueue({task.outputPath, task.textureId, success});

    if (success) {
        SPDLOG_INFO("Successfully generated texture: {}", task.outputPath.c_str());
    }
    else {
        SPDLOG_ERROR("Failed to generate texture: {}", task.outputPath.c_str());
    }

    task.Clear();
    bool removed = textureGenerateAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    ctx->bShouldRescanResources.store(true, std::memory_order_release);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AssetGenerator::OnEnvironmentGenerateComplete(bool success, EnvironmentMapGenerateSlotHandle slotHandle)
{
    ZoneScoped;

    if (!environmentMapGenerateAllocator.IsValid(slotHandle)) {
        SPDLOG_ERROR("OnEnvironmentGenerateComplete called with invalid slot handle");
        return;
    }

    EnvironmentMapGenerateSlot& task = environmentMapeGenerateTasks[slotHandle.index];
    environmentMapGenerateCompleteQueue.enqueue({task.outputPath, task.environmentMapId, success});

    if (success) {
        SPDLOG_INFO("Successfully generated environment map: {}", task.outputPath.c_str());
    }
    else {
        SPDLOG_ERROR("Failed to generate environment map: {}", task.outputPath.c_str());
    }

    task.Clear();
    bool removed = environmentMapGenerateAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    ctx->bShouldRescanResources.store(true, std::memory_order_release);

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}
} // namespace Editor
