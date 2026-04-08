//
// Created by William on 2026-02-02.
//

#ifndef WILL_ENGINE_MODEL_GENERATE_TASK_H
#define WILL_ENGINE_MODEL_GENERATE_TASK_H

#include <functional>
#include <semaphore>
#include <fastgltf/types.hpp>
#include <TaskScheduler.h>

#include "asset_generation_types.h"
#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"

#include "core/containers/inline_path.h"
#include "core/containers/span.h"
#include "core/memory/linear_allocator.h"
#include "core/memory/memory_manager.h"
#include "core/memory/tlsf_allocator.h"
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
        Core::MemoryManager* _memoryManager,
        enki::TaskScheduler* _scheduler,
        AssetGenerator* _generator,
        StaticModelGenerationProgress* _progress,
        Core::InlineFunction<void(bool success, ModelGenerateSlotHandle slotHandle)> notifyCallback
    );

    void Launch(ModelGenerateSlotHandle slotHandle, const Core::Path& gltfPath, const Core::Path& outputPath, uint64_t modelId);

    void Clear();

    Core::Path gltfPath;
    Core::Path outputPath;
    uint64_t modelId{0};

private:
    struct GenerateTask : enki::ITaskSet
    {
        StaticModelGenerateSlot* taskSlot{nullptr};

        explicit GenerateTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    bool LoadGltf();

    bool WriteStaticModel();

    /**
     *
     * @param nodes
     * @param oldToNew used to remap animations so the nodes make sense.
     */
    void TopologicalSortNodes(Core::Span<Engine::Node> nodes, Core::Span<uint32_t> oldToNew);

    static VkFilter ExtractFilter(fastgltf::Filter filter);

    static VkSamplerMipmapMode ExtractMipmapMode(fastgltf::Filter filter);

    static MaterialProperties ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial);

    static void LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform);

    static Vec4 GenerateBoundingSphere(Core::Span<Vertex> vertices);

    Core::MemoryManager* memoryManager{};
    enki::TaskScheduler* scheduler{};
    AssetGenerator* generator{};
    StaticModelGenerationProgress* progress{};

    Core::InlineFunction<void(bool success, ModelGenerateSlotHandle slotHandle)> _notifyCallback;

    ModelGenerateSlotHandle slotHandle = ModelGenerateSlotHandle::INVALID;
    GenerateTask task{};

    RawStaticModel rawModel;
};
} // Render

#endif //WILL_ENGINE_MODEL_GENERATE_TASK_H
