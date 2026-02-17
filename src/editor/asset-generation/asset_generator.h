#ifndef WILL_ENGINE_ASSET_GENERATOR_H
#define WILL_ENGINE_ASSET_GENERATOR_H

#include <filesystem>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <thread>
#include <concurrentqueue/concurrentqueue.h>

#include "asset_generation_types.h"
#include "environment_map_generate_slot.h"
#include "model_generate_slot.h"
#include "TaskScheduler.h"
#include "texture_generate_slot.h"
#include "core/allocators/lock_free_handle_allocator.h"

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
struct WillModelGenerationProgress
{
    enum LoadingProgress : uint32_t
    {
        NONE = 0,
        LOADING_GLTF,
        WRITING_WILL_MODEL,
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
};

struct ModelGenerateComplete
{
    std::filesystem::path outputPath;
    bool success;
};

struct TextureGenerateRequest {
    std::filesystem::path imagePath;
    std::filesystem::path outputPath;
    bool mipmapped;
    DXGI_FORMAT targetFormat;
};

struct TextureGenerateComplete {
    std::filesystem::path outputPath;
    bool success;
};

struct EnvironmentMapGenerateRequest {
    std::filesystem::path imagePath;
    std::filesystem::path outputPath;
};

struct EnvironmentMapGenerateComplete {
    std::filesystem::path outputPath;
    bool success;
};

using ModelGenerateSlotHandle = Core::Handle<ModelGenerateSlot>;

class AssetGenerator
{
public:
    AssetGenerator(Render::VulkanContext* context, Render::RenderThread* renderThread, AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager);
    ~AssetGenerator();

    void RequestModelGenerate(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);
    bool TryDequeueModelGenerateComplete(ModelGenerateComplete& outResult);
    void RequestTextureGenerate(const std::filesystem::path& imagePath, const std::filesystem::path& outputPath, bool mipmapped = true, DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM);
    bool TryDequeueTextureGenerateComplete(TextureGenerateComplete& outResult);
    void RequestEnvironmentMapGenerate(const std::filesystem::path& hdriPath, const std::filesystem::path& outputPath);
    bool TryDequeueCubemapGenerateComplete(EnvironmentMapGenerateComplete& outResult);
    void GenerateBRDFLUT(std::filesystem::path outputFile) const;

    const WillModelGenerationProgress& GetModelGenerationProgress() const { return modelGenerationProgress; }

    void Join();

private:
    friend class ModelGenerateSlot;

    void ThreadMain();
    void OnModelGenerateComplete(bool success, ModelGenerateSlotHandle slotHandle);
    void OnTextureGenerateComplete(bool success, TextureGenerateSlotHandle slotHandle);
    void OnEnvironmentGenerateComplete(bool success, EnvironmentMapGenerateSlotHandle slotHandle);

    void TransferQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const;
    void GraphicsQueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) const;

    Render::VulkanContext* context;
    Render::RenderThread* renderThread;
    AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager;
    std::unique_ptr<enki::TaskScheduler> assetGeneratorScheduler;

    std::array<ModelGenerateSlot, MODEL_GENERATION_JOB_COUNT> modelGenerateTasks;
    Core::LockFreeHandleAllocator<ModelGenerateSlot, MODEL_GENERATION_JOB_COUNT> modelGenerateAllocator;

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

    WillModelGenerationProgress modelGenerationProgress{};
};

} // Render

#endif //WILL_ENGINE_ASSET_GENERATOR_H