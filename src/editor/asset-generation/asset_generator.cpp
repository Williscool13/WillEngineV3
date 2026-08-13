//
// Created by William on 2025-12-15.
//

#include "asset_generator.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "miscellaneous_asset_generate.h"
#include "render/gpu_dispatcher.h"
#include "engine/resources/environment_map/environment_map_format.h"
#include "engine/resources/model/model_format.h"
#include "engine/resources/texture/texture_format.h"
#include "engine/include/engine_context.h"
#include "engine/resources/font/font_format.h"
#include "platform/file_utils.h"
#include "platform/thread_utils.h"
#include "render/render_thread.h"


namespace Editor
{
AssetGenerator::AssetGenerator(Core::MemoryManager& memoryManager,
                               Engine::EngineContext* ctx,
                               Render::VulkanContext* vulkanContext,
                               Render::RenderThread* renderThread,
                               Render::GPUDispatcher* gpuDispatcher,
                               enki::TaskScheduler* scheduler)
    : memoryManager(&memoryManager),
      ctx(ctx),
      vk(vulkanContext),
      renderThread(renderThread),
      gpuDispatcher(gpuDispatcher),
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
            this,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                GraphicsQueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, TextureGenerateSlotHandle slotHandle) {
                OnTextureGenerateComplete(success, slotHandle);
            }
        );
    }
    for (int32_t i = 0; i < FONT_GENERATION_JOB_COUNT; ++i) {
        fontGenerateTasks[i].Initialize(
            scheduler,
            &memoryManager,
            [this](bool success, FontGenerateSlotHandle slotHandle) {
                OnFontGenerateComplete(success, slotHandle);
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
                    task.Launch(slotHandle, req.gltfPath, req.outputPath, req.textureOutputPath, req.modelId, req.contentVersion, req.bSkipExistingTextures);
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
                                              req.targetFormat, req.contentVersion, req.category, req.ownerModelId, req.ownerImageIndex);
                    }
                    else {
                        task.Launch(slotHandle, req.imagePath, req.outputPath, req.textureId, req.mipmapped, req.targetFormat, req.contentVersion, req.flipY, req.declaredName, req.recipeSource, req.category,
                                    req.ownerModelId, req.ownerImageIndex);
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
                    task.Launch(slotHandle, req.imagePath, req.outputPath, req.environmentMapId, req.contentVersion);
                }
                else {
                    environmentMapGenerateRequestQueue.enqueue(req);
                }
            }
        }

        {
            ZoneScopedN("Process Probe Assemble Requests");
            ProbeAssembleRequest req{};
            if (probeAssembleRequestQueue.try_dequeue(req)) {
                Core::Handle<EnvironmentMapGenerateSlot> slotHandle = environmentMapGenerateAllocator.Add();
                if (slotHandle.IsValid()) {
                    EnvironmentMapGenerateSlot& task = environmentMapeGenerateTasks[slotHandle.index];
                    task.LaunchProbe(slotHandle, req.faces, req.captureSize, req.targetResolution, req.outputPath, req.environmentMapId, req.probeId, req.snapshot, req.contentVersion);
                }
                else {
                    probeAssembleRequestQueue.enqueue(std::move(req));
                }
            }
        }

        {
            ZoneScopedN("Process Font Generation Requests");
            FontGenerateRequest req{};
            if (fontGenerateRequestQueue.try_dequeue(req)) {
                Core::Handle<FontGenerateSlot> slotHandle = fontGenerateAllocator.Add();
                if (slotHandle.IsValid()) {
                    FontGenerateSlot& task = fontGenerateTasks[slotHandle.index];
                    task.Launch(slotHandle, req.ttfPath, req.outputPath, req.fontId, req.contentVersion);
                }
                else {
                    fontGenerateRequestQueue.enqueue(std::move(req));
                }
            }
        }

        if (workCounter.load(std::memory_order_acquire) > 0) {
            workCounter.fetch_sub(1);
        }
        else {
            ZoneScopedN("Idle - Waiting for Work");
            std::unique_lock lock(wakeMutex);
            // Timed wait: slot-full requeues don't bump workCounter, so guarantee a periodic re-poll
            wakeCV.wait_for(lock, std::chrono::milliseconds(10), [&] {
                return workCounter.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (workCounter.load(std::memory_order_acquire) > 0) {
                workCounter.fetch_sub(1);
            }
        }
    }
}

void AssetGenerator::BeginShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AssetGenerator::Join()
{
    BeginShutdown();
    thisThread.join();
}

void AssetGenerator::TransferQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const
{
    gpuDispatcher->Enqueue(Render::DispatchChannel::Transfer, cmd, fence, completionSignal);
}

void AssetGenerator::GraphicsQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const
{
    gpuDispatcher->Enqueue(Render::DispatchChannel::Graphics, cmd, fence, completionSignal);
}

void AssetGenerator::RequestModelGenerate(const Core::Path& gltfPath, const Core::Path& outputPath, const Core::Path& textureOutputPath, bool bSkipExistingTextures)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
    uint64_t modelId = modelIdRng();
    uint64_t contentVersion = 1;
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWStaticModelHeaderAnyVersion(outputPath)) {
            modelId = header->modelId;
            contentVersion = header->contentVersion + 1;
        }
    }
    modelGenerateRequestQueue.enqueue({gltfPath, outputPath, textureOutputPath, modelId, contentVersion, bSkipExistingTextures});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

AssetGenerator::RecoveredTextureIdentity AssetGenerator::RecoverTextureIdentity(const Core::Path& outputPath, uint64_t ownerModelId, uint32_t ownerImageIndex)
{
    RecoveredTextureIdentity out{Engine::TextureID{textureIdRng()}};
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWTextureHeaderAnyVersion(outputPath)) {
            out.id = Engine::TextureID{header->textureId};
            out.contentVersion = header->contentVersion + 1;
            out.declaredName = Core::InlineString<128>(header->name);
            out.recipeSource = Core::InlineString<256>(header->genSource);
        }
        return out;
    }
    if (ownerModelId != 0) {
        Core::Vector<Core::Path> files(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
        Platform::RecursiveDirectoryIterator(outputPath.Parent(), files);
        for (const Core::Path& path : files) {
            if (path.Extension() != ".wtexture") { continue; }
            auto header = Engine::ReadWTextureHeaderAnyVersion(path);
            if (header && header->ownerModelId == ownerModelId && header->ownerImageIndex == ownerImageIndex) {
                out.id = Engine::TextureID{header->textureId};
                out.contentVersion = header->contentVersion + 1;
                break;
            }
        }
    }
    return out;
}

Engine::TextureID AssetGenerator::RequestTextureGenerateFromFile(const Core::Path& imagePath, const Core::Path& outputPath, bool mipmapped, DXGI_FORMAT targetFormat, bool flipY, Engine::TextureCategory category,
                                                                 uint64_t ownerModelId, uint32_t ownerImageIndex)
{
    ZoneScoped;
    if (bShouldExit.load(std::memory_order_acquire)) {
        return {};
    }
    const RecoveredTextureIdentity identity = RecoverTextureIdentity(outputPath, ownerModelId, ownerImageIndex);
    TextureGenerateRequest req{};
    req.outputPath = outputPath;
    req.textureId = identity.id;
    req.mipmapped = mipmapped;
    req.flipY = flipY;
    req.targetFormat = targetFormat;
    req.contentVersion = identity.contentVersion;
    req.declaredName = identity.declaredName;
    req.recipeSource = identity.recipeSource;
    req.category = category;
    req.ownerModelId = ownerModelId;
    req.ownerImageIndex = ownerImageIndex;
    req.imagePath = imagePath;
    textureGenerateRequestQueue.enqueue(std::move(req));
    workCounter.fetch_add(1);
    wakeCV.notify_one();
    return identity.id;
}

Engine::TextureID AssetGenerator::RequestTextureGenerateFromMemory(Core::HeapArray<uint8_t> pixels, uint32_t w, uint32_t h, uint32_t bytesPerPixel, const Core::Path& outputPath,
                                                                   bool mipmapped, DXGI_FORMAT targetFormat, Engine::TextureCategory category,
                                                                   uint64_t ownerModelId, uint32_t ownerImageIndex)
{
    ZoneScoped;
    if (bShouldExit.load(std::memory_order_acquire)) {
        return {};
    }
    const RecoveredTextureIdentity identity = RecoverTextureIdentity(outputPath, ownerModelId, ownerImageIndex);
    TextureGenerateRequest req{};
    req.outputPath = outputPath;
    req.textureId = identity.id;
    req.mipmapped = mipmapped;
    req.targetFormat = targetFormat;
    req.contentVersion = identity.contentVersion;
    req.category = category;
    req.ownerModelId = ownerModelId;
    req.ownerImageIndex = ownerImageIndex;
    req.sourcePixels = std::move(pixels);
    req.sourceWidth = w;
    req.sourceHeight = h;
    req.sourceBytesPerPixel = bytesPerPixel;
    textureGenerateRequestQueue.enqueue(std::move(req));
    workCounter.fetch_add(1);
    wakeCV.notify_one();
    return identity.id;
}

void AssetGenerator::RequestEnvironmentMapGenerate(const Core::Path& hdriPath, const Core::Path& outputPath)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
    Engine::EnvironmentMapID id{environmentMapIdRng()};
    uint64_t contentVersion = 1;
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWEnvMapHeaderAnyVersion(outputPath)) {
            id = Engine::EnvironmentMapID{header->environmentMapId};
            contentVersion = header->contentVersion + 1;
        }
    }
    environmentMapGenerateRequestQueue.enqueue({hdriPath, outputPath, id, contentVersion});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AssetGenerator::RequestProbeAssemble(Core::HeapArray<uint16_t>* faces, uint32_t captureSize, uint32_t targetResolution, const Core::Path& outputPath, uint64_t probeId, const Engine::ProbeBakeSnapshot& snapshot)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return;
    }
    Engine::EnvironmentMapID id{environmentMapIdRng()};
    uint64_t contentVersion = 1;
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWProbeHeaderAnyVersion(outputPath)) {
            id = Engine::EnvironmentMapID{header->environmentMapId};
            contentVersion = header->contentVersion + 1;
        }
    }

    ProbeAssembleRequest req{};
    for (uint32_t face = 0; face < 6; ++face) {
        req.faces[face] = std::move(faces[face]);
    }
    req.captureSize = captureSize;
    req.targetResolution = targetResolution;
    req.outputPath = outputPath;
    req.environmentMapId = id;
    req.probeId = probeId;
    req.snapshot = snapshot;
    req.contentVersion = contentVersion;
    probeAssembleRequestQueue.enqueue(std::move(req));
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

void AssetGenerator::GenerateBlueNoiseTexture(const Core::Path& outputFile)
{
    CreateBlueNoiseTexture(memoryManager, outputFile, Engine::TextureID(textureIdRng()));
}

void AssetGenerator::OnModelGenerateComplete(bool success, ModelGenerateSlotHandle slotHandle)
{
    ZoneScoped;

    if (!modelGenerateAllocator.IsValid(slotHandle)) {
        SPDLOG_ERROR("OnModelGenerateComplete called with invalid slot handle");
        return;
    }

    StaticModelGenerateSlot& task = modelGenerateTasks[slotHandle.index];
    modelGenerateCompleteQueue.enqueue({task.outputPath, Engine::ModelID{task.modelId}, success});

    if (success) {
        SPDLOG_INFO("Successfully generated model: {}", task.outputPath.c_str());
    }
    else {
        SPDLOG_ERROR("Failed to generate model: {}", task.outputPath.c_str());
    }

    task.Clear();
    bool removed = modelGenerateAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

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

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

Engine::FontID AssetGenerator::RequestFontGenerate(const Core::Path& ttfPath, const Core::Path& outputPath)
{
    ZoneScoped;

    if (bShouldExit.load(std::memory_order_acquire)) {
        return {};
    }
    Engine::FontID id{fontIdRng()};
    uint64_t contentVersion = 1;
    if (outputPath.Exists()) {
        if (auto header = Engine::ReadWFontHeaderAnyVersion(outputPath)) {
            id = Engine::FontID{header->fontId};
            contentVersion = header->contentVersion + 1;
        }
    }
    fontGenerateRequestQueue.enqueue({ttfPath, outputPath, id, contentVersion});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
    return id;
}

bool AssetGenerator::TryDequeueFontGenerateComplete(FontGenerateComplete& outResult)
{
    return fontGenerateCompleteQueue.try_dequeue(outResult);
}

void AssetGenerator::OnFontGenerateComplete(bool success, FontGenerateSlotHandle slotHandle)
{
    ZoneScoped;

    if (!fontGenerateAllocator.IsValid(slotHandle)) {
        SPDLOG_ERROR("OnFontGenerateComplete called with invalid slot handle");
        return;
    }

    FontGenerateSlot& task = fontGenerateTasks[slotHandle.index];
    fontGenerateCompleteQueue.enqueue({task.outputPath, task.fontId, success});

    if (success) {
        SPDLOG_INFO("Successfully generated font: {}", task.outputPath.c_str());
    }
    else {
        SPDLOG_ERROR("Failed to generate font: {}", task.outputPath.c_str());
    }

    task.Clear();
    bool removed = fontGenerateAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

} // namespace Editor
