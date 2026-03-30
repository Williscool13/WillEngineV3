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
#include "asset-load/asset_load_types.h"

#include "core/memory/linear_allocator.h"
#include "engine/resources/model/model_types.h"

namespace Editor
{
class AssetGenerator;
struct StaticModelGenerationProgress;
struct AssetGeneratorImmediateParameters;

class StaticModelGenerateSlot
{
public:
    using ModelGenerateSlotHandle = Core::Handle<StaticModelGenerateSlot>;

    StaticModelGenerateSlot();

    ~StaticModelGenerateSlot();

    void Initialize(
        int32_t slotIndex,
        enki::TaskScheduler* _scheduler,
        AssetGenerator* _generator,
        StaticModelGenerationProgress* _progress,
        std::function<void(bool success, ModelGenerateSlotHandle slotHandle)> notifyCallback
    );

    void Launch(ModelGenerateSlotHandle slotHandle, const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath, uint64_t modelId);

    void Clear();

    std::filesystem::path gltfPath;
    std::filesystem::path outputPath;
    uint64_t modelId{0};

private:
    struct GenerateTask : enki::ITaskSet
    {
        StaticModelGenerateSlot* taskSlot{nullptr};

        explicit GenerateTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    bool LoadGltf();

    bool WriteStaticModel();

    void TopologicalSortNodes(std::vector<Engine::Node>& nodes, std::vector<uint32_t>& oldToNew);

    static VkFilter ExtractFilter(fastgltf::Filter filter);

    static VkSamplerMipmapMode ExtractMipmapMode(fastgltf::Filter filter);

    static MaterialProperties ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial);

    static void LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform);

    static glm::vec4 GenerateBoundingSphere(const std::vector<Vertex>& vertices);

    enki::TaskScheduler* scheduler{};
    AssetGenerator* generator{};
    StaticModelGenerationProgress* progress{};

    std::function<void(bool success, ModelGenerateSlotHandle slotHandle)> _notifyCallback;

    ModelGenerateSlotHandle slotHandle = ModelGenerateSlotHandle::INVALID;
    std::unique_ptr<GenerateTask> task;

    AssetLoad::RawStaticModel rawModel;
    std::vector<Engine::Node> sortedNodes;
    std::vector<bool> visited;
};
} // Render

#endif //WILL_ENGINE_MODEL_GENERATE_TASK_H
