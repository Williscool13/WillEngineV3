//
// Created by William on 2026-02-02.
//

#ifndef WILL_ENGINE_MODEL_GENERATE_TASK_H
#define WILL_ENGINE_MODEL_GENERATE_TASK_H

#include <filesystem>
#include <functional>
#include <semaphore>
#include <fastgltf/types.hpp>
#include <TaskScheduler.h>

#include "asset_generation_types.h"

#include "core/allocators/linear_allocator.h"

namespace Editor
{
class AssetGenerator;
struct WillModelGenerationProgress;
struct AssetGeneratorImmediateParameters;

class ModelGenerateSlot
{
public:
    using ModelGenerateSlotHandle = Core::Handle<ModelGenerateSlot>;

    ModelGenerateSlot();

    ~ModelGenerateSlot();

    void Initialize(
        int32_t slotIndex,
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        WillModelGenerationProgress* _progress,
        std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback, std::function<void(bool success, ModelGenerateSlotHandle slotHandle)>
        notifyCallback
    );

    void Launch(ModelGenerateSlotHandle slotHandle, const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);

    void Clear();

    std::filesystem::path gltfPath;
    std::filesystem::path outputPath;

private:
    struct GenerateTask : enki::ITaskSet
    {
        ModelGenerateSlot* taskSlot{nullptr};

        explicit GenerateTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    bool LoadGltf(VkCommandBuffer cmd, const std::function<void()>& startRecording, const std::function<void(bool)>& submitAndWait);

    bool WriteWillModel(VkCommandBuffer cmd, const std::function<void()>& startRecording, const std::function<void(bool)>& submitAndWait);

    void TopologicalSortNodes(std::vector<Render::Node>& nodes, std::vector<uint32_t>& oldToNew);

    static Render::AllocatedImage RecordCreateImageFromData(Render::VulkanContext* context, VkCommandBuffer cmd, Render::AllocatedBuffer& stagingBuffer, size_t offset, unsigned char* data,
                                                            size_t size,
                                                            VkExtent3D imageExtent, VkFormat format, VkImageUsageFlagBits usage);

    static VkFilter ExtractFilter(fastgltf::Filter filter);

    static VkSamplerMipmapMode ExtractMipmapMode(fastgltf::Filter filter);

    static MaterialProperties ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial);

    static void LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform);

    static glm::vec4 GenerateBoundingSphere(const std::vector<Vertex>& vertices);

    enki::TaskScheduler* scheduler{};
    Render::VulkanContext* context{};
    WillModelGenerationProgress* progress{};

    std::filesystem::path temporaryPath{};

    Render::AllocatedBuffer imageStagingBuffer{};
    Core::LinearAllocator imageStagingAllocator{MODEL_GENERATION_STAGING_BUFFER_SIZE};
    Render::AllocatedBuffer imageReceivingBuffer{};
    Core::LinearAllocator imageReceivingAllocator{MODEL_GENERATION_STAGING_BUFFER_SIZE};

    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _graphicsDispatchCallback;
    std::function<void(bool success, ModelGenerateSlotHandle slotHandle)> _notifyCallback;

    ModelGenerateSlotHandle slotHandle = ModelGenerateSlotHandle::INVALID;
    std::unique_ptr<GenerateTask> task;

    RawGltfModel rawModel;
    std::vector<Render::Node> sortedNodes;
    std::vector<bool> visited;
};

void WriteModelBinary(std::ofstream& file, const RawGltfModel& model);
} // Render

#endif //WILL_ENGINE_MODEL_GENERATE_TASK_H
