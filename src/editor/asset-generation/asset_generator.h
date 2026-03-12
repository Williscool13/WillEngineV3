#ifndef WILL_ENGINE_ASSET_GENERATOR_H
#define WILL_ENGINE_ASSET_GENERATOR_H

#include <filesystem>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <random>
#include <semaphore>
#include <thread>
#include <concurrentqueue/concurrentqueue.h>

#include "engine/core/texture_id.h"

#include "asset_generation_types.h"
#include "environment_map_generate_slot.h"
#include "static_model_generate_slot.h"
#include "TaskScheduler.h"
#include "texture_generate_slot.h"
#include "core/allocators/lock_free_handle_allocator.h"

namespace Core
{
struct EngineContext;
}

namespace Render
{
class RenderThread;
}

namespace AssetLoad
{
class AsyncAssetLoadManager;
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
    std::filesystem::path gltfPath;
    std::filesystem::path outputPath;
    uint64_t modelId{0};
};

struct ModelGenerateComplete
{
    std::filesystem::path outputPath;
    bool success;
};

struct TextureGenerateRequest
{
    std::filesystem::path outputPath;
    Engine::TextureID textureId;
    bool mipmapped;
    DXGI_FORMAT targetFormat;

    std::filesystem::path imagePath;

    // Optional: pre-loaded pixel data (takes priority over imagePath)
    std::unique_ptr<uint8_t[]> sourcePixels;
    uint32_t sourceWidth{0};
    uint32_t sourceHeight{0};
    uint32_t sourceBytesPerPixel{0};
};

struct TextureGenerateComplete
{
    std::filesystem::path outputPath;
    Engine::TextureID textureId;
    bool success;
};

struct EnvironmentMapGenerateRequest
{
    std::filesystem::path imagePath;
    std::filesystem::path outputPath;
};

struct EnvironmentMapGenerateComplete
{
    std::filesystem::path outputPath;
    bool success;
};

using ModelGenerateSlotHandle = Core::Handle<StaticModelGenerateSlot>;

class AssetGenerator
{
public:
    AssetGenerator(Core::EngineContext* ctx, Render::VulkanContext* vk, Render::RenderThread* renderThread, AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager);

    ~AssetGenerator();

    void RequestModelGenerate(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);

    bool TryDequeueModelGenerateComplete(ModelGenerateComplete& outResult);

    Engine::TextureID RequestTextureGenerateFromFile(const std::filesystem::path& imagePath, const std::filesystem::path& outputPath, bool mipmapped = true,
                                                     DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM);

    Engine::TextureID RequestTextureGenerateFromMemory(std::unique_ptr<uint8_t[]> pixels, uint32_t w, uint32_t h, uint32_t bytesPerPixel, const std::filesystem::path& outputPath,
                                                       bool mipmapped = false, DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM);

    bool TryDequeueTextureGenerateComplete(TextureGenerateComplete& outResult);

    void RequestEnvironmentMapGenerate(const std::filesystem::path& hdriPath, const std::filesystem::path& outputPath);

    bool TryDequeueCubemapGenerateComplete(EnvironmentMapGenerateComplete& outResult);

    void GenerateBRDFLUT(const std::filesystem::path& outputFile);

    const std::array<StaticModelGenerationProgress, MODEL_GENERATION_JOB_COUNT>& GetModelGenerationProgresses() const { return modelGenerationProgress; }
    const std::filesystem::path& GetModelGenerateSlotPath(uint32_t index) const { return modelGenerateTasks[index].gltfPath; }

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

    void Join();

private:
    friend class StaticModelGenerateSlot;

    void ThreadMain();

    void OnModelGenerateComplete(bool success, ModelGenerateSlotHandle slotHandle);

    void OnTextureGenerateComplete(bool success, TextureGenerateSlotHandle slotHandle);

    void OnEnvironmentGenerateComplete(bool success, EnvironmentMapGenerateSlotHandle slotHandle);

    void TransferQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const;

    void GraphicsQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const;

    Core::EngineContext* ctx;
    Render::VulkanContext* vk;
    Render::RenderThread* renderThread;
    AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager;
    std::unique_ptr<enki::TaskScheduler> assetGeneratorScheduler;

    std::mt19937_64 modelIdRng{std::random_device{}()};
    std::mt19937_64 textureIdRng{std::random_device{}()};

    std::array<StaticModelGenerateSlot, MODEL_GENERATION_JOB_COUNT> modelGenerateTasks;
    Core::LockFreeHandleAllocator<StaticModelGenerateSlot, MODEL_GENERATION_JOB_COUNT> modelGenerateAllocator;

    std::array<TextureGenerateSlot, TEXTURE_GENERATION_JOB_COUNT> textureGenerateTasks;
    Core::LockFreeHandleAllocator<TextureGenerateSlot, TEXTURE_GENERATION_JOB_COUNT> textureGenerateAllocator;

    std::array<EnvironmentMapGenerateSlot, ENVIRONMENT_MAP_GENERATION_JOB_COUNT> environmentMapeGenerateTasks;
    Core::LockFreeHandleAllocator<EnvironmentMapGenerateSlot, ENVIRONMENT_MAP_GENERATION_JOB_COUNT> environmentMapGenerateAllocator;

    moodycamel::ConcurrentQueue<ModelGenerateRequest> modelGenerateRequestQueue;
    moodycamel::ConcurrentQueue<ModelGenerateComplete> modelGenerateCompleteQueue;

    moodycamel::ConcurrentQueue<TextureGenerateRequest> textureGenerateRequestQueue;
    moodycamel::ConcurrentQueue<TextureGenerateComplete> textureGenerateCompleteQueue;

    moodycamel::ConcurrentQueue<EnvironmentMapGenerateRequest> environmentMapGenerateRequestQueue;
    moodycamel::ConcurrentQueue<EnvironmentMapGenerateComplete> environmentMapGenerateCompleteQueue;

    std::atomic<bool> bShouldExit{false};
    std::atomic<uint32_t> workCounter{0};
    std::mutex wakeMutex;
    std::condition_variable wakeCV;
    std::jthread thisThread;

    std::array<StaticModelGenerationProgress, MODEL_GENERATION_JOB_COUNT> modelGenerationProgress{};
};
} // Render

#endif //WILL_ENGINE_ASSET_GENERATOR_H
