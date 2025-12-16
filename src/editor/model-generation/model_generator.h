//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_GENERATOR_H
#define WILL_ENGINE_MODEL_GENERATOR_H
#include <filesystem>

#include "model_generation_types.h"
#include "offsetAllocator.hpp"
#include "TaskScheduler.h"
#include "fastgltf/types.hpp"

namespace Render
{
struct WillModelGenerationProgress
{
    std::atomic<bool> bIsGenerating{false};
    std::atomic<float> progress{0.0f}; // 0.0 to 1.0
    std::string currentStage{};
    std::mutex stageMutex{};
};

constexpr uint32_t MODEL_GENERATION_STAGING_BUFFER_SIZE = 2 * 64 * 1024 * 1024; // 2 x 64 MB (1x uncompressed 4k rgba8, or 4x 4k BC7)

class ModelGenerator
{
public:
    ModelGenerator(VulkanContext* context, enki::TaskScheduler* taskscheduler);

    ~ModelGenerator();

    void GenerateWillModelAsync(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);

    bool GenerateWillModel(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);

    const WillModelGenerationProgress& GetProgress() const { return generationProgress; }

private:
    struct GenerateTask : enki::ITaskSet
    {
        ModelGenerator* generator;
        std::filesystem::path gltfPath;
        std::filesystem::path outputPath;

        explicit GenerateTask(ModelGenerator* gen)
            : ITaskSet(1), generator(gen)
        {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
        {
            generator->UpdateProgress(0.1f, "Loading GLTF");
            RawGltfSkinnedModel rawModel = generator->LoadGltf(generator->generationProgress, gltfPath);

            if (!rawModel.bSuccessfullyLoaded) {
                generator->UpdateProgress(0.0f, "Failed to load GLTF");
                generator->generationProgress.bIsGenerating = false;
                return;
            }

            generator->UpdateProgress(0.7f, "Writing .willmodel");
            bool success = generator->WriteWillModel(generator->generationProgress, rawModel, outputPath);

            generator->UpdateProgress(1.0f, success ? "Complete" : "Failed to write");
            generator->generationProgress.bIsGenerating = false;
        }
    };

    RawGltfSkinnedModel LoadGltf(WillModelGenerationProgress& progress, const std::filesystem::path& source);

    bool WriteWillModel(WillModelGenerationProgress& progress, const RawGltfSkinnedModel& rawModel, const std::filesystem::path& outputPath);

    void UpdateProgress(float progress, const std::string& stage);

private:
    static VkFilter ExtractFilter(fastgltf::Filter filter);

    static VkSamplerMipmapMode ExtractMipmapMode(fastgltf::Filter filter);

    static MaterialProperties ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial);

    static void LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform);

    static glm::vec4 GenerateBoundingSphere(const std::vector<Vertex>& vertices);

    static glm::vec4 GenerateBoundingSphere(const std::vector<SkinnedVertex>& vertices);

    void TopologicalSortNodes(std::vector<Node>& nodes, std::vector<uint32_t>& oldToNew);

    AllocatedImage RecordCreateImageFromData(VkCommandBuffer cmd, size_t offset, unsigned char* data, size_t size, VkExtent3D imageExtent, VkFormat format, VkImageUsageFlagBits usage, bool mipmapped);

private:
    VulkanContext* context{};
    enki::TaskScheduler* taskscheduler{};
    WillModelGenerationProgress generationProgress{};
    GenerateTask generateTask;

    VkFence immFence{VK_NULL_HANDLE};
    VkCommandPool immCommandPool{VK_NULL_HANDLE};
    VkCommandBuffer immCommandBuffer{VK_NULL_HANDLE};

    OffsetAllocator::Allocator imageStagingAllocator{MODEL_GENERATION_STAGING_BUFFER_SIZE};
    AllocatedBuffer imageStagingBuffer{};
    AllocatedBuffer imageReceivingBuffer{};

private: // Cache
    std::vector<Node> sortedNodes;
    std::vector<bool> visited;
};

void WriteModelBinary(std::ofstream& file, const RawGltfModel& model);

void WriteModelBinary(std::ofstream& file, const RawGltfSkinnedModel& model);
} // Render

#endif //WILL_ENGINE_MODEL_GENERATOR_H
