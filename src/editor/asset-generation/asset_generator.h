#ifndef WILL_ENGINE_ASSET_GENERATOR_H
#define WILL_ENGINE_ASSET_GENERATOR_H

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <random>
#include <semaphore>
#include <thread>
#include <concurrentqueue/concurrentqueue.h>

#include "core/containers/inline_path.h"
#include "engine/core/environment_map_id.h"
#include "engine/core/model_id.h"
#include "engine/core/texture_id.h"
#include "engine/resources/environment_map/probe_format.h"

#include "asset_generation_types.h"
#include "environment_map_generate_slot.h"
#include "font_generate_slot.h"
#include "static_model_generate_slot.h"
#include "TaskScheduler.h"
#include "texture_generate_slot.h"
#include "core/containers/array.h"
#include "core/memory/lock_free_handle_allocator.h"
#include "core/memory/memory_manager.h"
#include "engine/core/font_id.h"

namespace Engine
{
struct EngineContext;
}

namespace Render
{
class GPUDispatcher;
class RenderThread;
}

namespace Editor
{
struct StaticModelGenerationProgress
{
    enum LoadingProgress : uint32_t
    {
        NONE = 0,
        LOADING_GLTF,
        WRITING_STATIC_MODEL,
        FAILED,
        SUCCESS,
    };

    std::atomic<LoadingProgress> loadingState{NONE};
    std::atomic<int32_t> value{0};
};

struct ModelGenerateRequest
{
    Core::Path gltfPath;
    Core::Path outputPath;
    Core::Path textureOutputPath;
    uint64_t modelId{0};
    uint64_t contentVersion{1};
    bool bSkipExistingTextures{false};
};

struct ModelGenerateComplete
{
    Core::Path outputPath;
    Engine::ModelID modelId{};
    bool success{};
};

struct TextureGenerateRequest
{
    Core::Path outputPath;
    Engine::TextureID textureId;
    bool mipmapped;
    bool flipY{true};
    DXGI_FORMAT targetFormat;
    uint64_t contentVersion{1};
    Core::InlineString<128> declaredName{};
    Core::InlineString<256> recipeSource{};
    Engine::TextureCategory category{Engine::TextureCategory::Standalone};
    uint64_t ownerModelId{0};
    uint32_t ownerImageIndex{UINT32_MAX};

    Core::Path imagePath;

    // Optional: pre-loaded pixel data (takes priority over imagePath)
    Core::HeapArray<uint8_t> sourcePixels;
    uint32_t sourceWidth{0};
    uint32_t sourceHeight{0};
    uint32_t sourceBytesPerPixel{0};
};

struct TextureGenerateComplete
{
    Core::Path outputPath;
    Engine::TextureID textureId;
    bool success;
};

struct EnvironmentMapGenerateRequest
{
    Core::Path imagePath;
    Core::Path outputPath;
    Engine::EnvironmentMapID environmentMapId{};
    uint64_t contentVersion{1};
};

struct EnvironmentMapGenerateComplete
{
    Core::Path outputPath;
    Engine::EnvironmentMapID environmentMapId{};
    bool success;
};

struct ProbeAssembleRequest
{
    Core::HeapArray<uint16_t> faces[6];
    uint32_t captureSize{0};
    uint32_t targetResolution{0};
    Core::Path outputPath;
    Engine::EnvironmentMapID environmentMapId{};
    uint64_t probeId{0};
    Engine::ProbeBakeSnapshot snapshot{};
    uint64_t contentVersion{1};
};

struct FontGenerateRequest
{
    Core::Path ttfPath;
    Core::Path outputPath;
    Engine::FontID fontId{};
    uint64_t contentVersion{1};
};

struct FontGenerateComplete
{
    Core::Path outputPath;
    Engine::FontID fontId{};
    bool success{};
};

using ModelGenerateSlotHandle = Core::Handle<StaticModelGenerateSlot>;

class AssetGenerator
{
public:
    AssetGenerator(
        Core::MemoryManager& memoryManager,
        Engine::EngineContext* ctx,
        Render::VulkanContext* vulkanContext,
        Render::RenderThread* renderThread,
        Render::GPUDispatcher* gpuDispatcher,
        enki::TaskScheduler* scheduler
    );

    ~AssetGenerator();

    void RequestModelGenerate(const Core::Path& gltfPath, const Core::Path& outputPath, const Core::Path& textureOutputPath = {}, bool bSkipExistingTextures = false);

    bool TryDequeueModelGenerateComplete(ModelGenerateComplete& outResult);

    Engine::TextureID RequestTextureGenerateFromFile(const Core::Path& imagePath, const Core::Path& outputPath, bool mipmapped = true,
                                                     DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM, bool flipY = true, Engine::TextureCategory category = Engine::TextureCategory::Standalone,
                                                     uint64_t ownerModelId = 0, uint32_t ownerImageIndex = UINT32_MAX);

    Engine::TextureID RequestTextureGenerateFromMemory(Core::HeapArray<uint8_t> pixels, uint32_t w, uint32_t h, uint32_t bytesPerPixel, const Core::Path& outputPath,
                                                       bool mipmapped = false, DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM, Engine::TextureCategory category = Engine::TextureCategory::Standalone,
                                                       uint64_t ownerModelId = 0, uint32_t ownerImageIndex = UINT32_MAX);

    bool TryDequeueTextureGenerateComplete(TextureGenerateComplete& outResult);

    void RequestEnvironmentMapGenerate(const Core::Path& hdriPath, const Core::Path& outputPath);

    /** Assembles the 6 captured probe faces (moved in) into a prefiltered .wprobe; completion is surfaced through the environment-map complete queue. */
    void RequestProbeAssemble(Core::HeapArray<uint16_t>* faces, uint32_t captureSize, uint32_t targetResolution, const Core::Path& outputPath, uint64_t probeId, const Engine::ProbeBakeSnapshot& snapshot);

    bool TryDequeueCubemapGenerateComplete(EnvironmentMapGenerateComplete& outResult);

    Engine::FontID RequestFontGenerate(const Core::Path& ttfPath, const Core::Path& outputPath);

    bool TryDequeueFontGenerateComplete(FontGenerateComplete& outResult);

    void GenerateBRDFLUT(const Core::Path& outputFile);

    void GenerateSMAATextures(const Core::Path& parentDirectory);

    void GenerateBlueNoiseTexture(const Core::Path& outputFile);

    const Core::Array<StaticModelGenerationProgress, MODEL_GENERATION_JOB_COUNT>& GetModelGenerationProgresses() const { return modelGenerationProgress; }
    const Core::Path& GetModelGenerateSlotPath(uint32_t index) const { return modelGenerateTasks[index].gltfPath; }

    [[nodiscard]] uint32_t GetTotalModelGenerateCount() const
    {
        return modelGenerateAllocator.GetCount() + modelGenerateRequestQueue.size_approx();
    }

    [[nodiscard]] uint32_t GetTotalTextureGenerateCount() const
    {
        return textureGenerateAllocator.GetCount() + textureGenerateRequestQueue.size_approx();
    }

    [[nodiscard]] uint32_t GetActiveModelGenerateCount() const
    {
        return modelGenerateAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActiveTextureGenerateCount() const
    {
        return textureGenerateAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetTotalEnvironmentMapGenerateCount() const
    {
        return environmentMapGenerateAllocator.GetCount() + environmentMapGenerateRequestQueue.size_approx();
    }

    [[nodiscard]] uint32_t GetActiveEnvironmentMapGenerateCount() const
    {
        return environmentMapGenerateAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetTotalFontGenerateCount() const
    {
        return fontGenerateAllocator.GetCount() + fontGenerateRequestQueue.size_approx();
    }

    [[nodiscard]] uint32_t GetActiveFontGenerateCount() const
    {
        return fontGenerateAllocator.GetCount();
    }

    void Join();

    bool GetFastMode() const { return bFastMode.load(std::memory_order_relaxed); }
    void SetFastMode(bool value) { bFastMode.store(value, std::memory_order_relaxed); }

private:
    friend class StaticModelGenerateSlot;

    struct RecoveredTextureIdentity
    {
        Engine::TextureID id{};
        uint64_t contentVersion{1};
        Core::InlineString<128> declaredName{};
        Core::InlineString<256> recipeSource{};
    };

    /**
     * Stable-identity recovery for a texture about to be regenerated
     */
    RecoveredTextureIdentity RecoverTextureIdentity(const Core::Path& outputPath, uint64_t ownerModelId, uint32_t ownerImageIndex);

    void ThreadMain();

    void OnModelGenerateComplete(bool success, ModelGenerateSlotHandle slotHandle);

    void OnTextureGenerateComplete(bool success, TextureGenerateSlotHandle slotHandle);

    void OnEnvironmentGenerateComplete(bool success, EnvironmentMapGenerateSlotHandle slotHandle);

    void OnFontGenerateComplete(bool success, FontGenerateSlotHandle slotHandle);

    void TransferQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const;

    void GraphicsQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const;

    Core::MemoryManager* memoryManager{};
    Engine::EngineContext* ctx;
    Render::VulkanContext* vk;
    Render::RenderThread* renderThread;
    Render::GPUDispatcher* gpuDispatcher;
    enki::TaskScheduler* scheduler{};

    std::mt19937_64 modelIdRng{std::random_device{}()};
    std::mt19937_64 textureIdRng{std::random_device{}()};
    std::mt19937_64 environmentMapIdRng{std::random_device{}()};
    std::mt19937_64 fontIdRng{std::random_device{}()};

    Core::Array<StaticModelGenerateSlot, MODEL_GENERATION_JOB_COUNT> modelGenerateTasks;
    Core::LockFreeHandleAllocator<StaticModelGenerateSlot, MODEL_GENERATION_JOB_COUNT> modelGenerateAllocator;

    Core::Array<TextureGenerateSlot, TEXTURE_GENERATION_JOB_COUNT> textureGenerateTasks;
    Core::LockFreeHandleAllocator<TextureGenerateSlot, TEXTURE_GENERATION_JOB_COUNT> textureGenerateAllocator;

    Core::Array<EnvironmentMapGenerateSlot, ENVIRONMENT_MAP_GENERATION_JOB_COUNT> environmentMapeGenerateTasks;
    Core::LockFreeHandleAllocator<EnvironmentMapGenerateSlot, ENVIRONMENT_MAP_GENERATION_JOB_COUNT> environmentMapGenerateAllocator;

    Core::Array<FontGenerateSlot, FONT_GENERATION_JOB_COUNT> fontGenerateTasks;
    Core::LockFreeHandleAllocator<FontGenerateSlot, FONT_GENERATION_JOB_COUNT> fontGenerateAllocator;

    moodycamel::ConcurrentQueue<ModelGenerateRequest> modelGenerateRequestQueue;
    moodycamel::ConcurrentQueue<ModelGenerateComplete> modelGenerateCompleteQueue;

    moodycamel::ConcurrentQueue<TextureGenerateRequest> textureGenerateRequestQueue;
    moodycamel::ConcurrentQueue<TextureGenerateComplete> textureGenerateCompleteQueue;

    moodycamel::ConcurrentQueue<EnvironmentMapGenerateRequest> environmentMapGenerateRequestQueue;
    moodycamel::ConcurrentQueue<EnvironmentMapGenerateComplete> environmentMapGenerateCompleteQueue;
    moodycamel::ConcurrentQueue<ProbeAssembleRequest> probeAssembleRequestQueue;

    moodycamel::ConcurrentQueue<FontGenerateRequest> fontGenerateRequestQueue;
    moodycamel::ConcurrentQueue<FontGenerateComplete> fontGenerateCompleteQueue;

    std::atomic<bool> bFastMode{true};
    std::atomic<bool> bShouldExit{false};
    std::atomic<uint32_t> workCounter{0};
    std::mutex wakeMutex;
    std::condition_variable wakeCV;
    std::jthread thisThread;


    // todo access to this is not thread safe. Needs to be mutex locked (maybe within?). Path should be within, and mutex locked (should be fine)
    Core::Array<StaticModelGenerationProgress, MODEL_GENERATION_JOB_COUNT> modelGenerationProgress{};
};
} // Render

#endif //WILL_ENGINE_ASSET_GENERATOR_H
