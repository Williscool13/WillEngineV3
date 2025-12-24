//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_GENERATOR_H
#define WILL_ENGINE_MODEL_GENERATOR_H
#include <filesystem>

#include "asset_generation_types.h"
#include "offsetAllocator.hpp"
#include "TaskScheduler.h"
#include "fastgltf/types.hpp"

namespace Render
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
    std::atomic<int32_t> value{0}; // out of 100
};

constexpr uint32_t MODEL_GENERATION_STAGING_BUFFER_SIZE = 2 * 64 * 1024 * 1024; // 2 x 64 MB (1x uncompressed 4k rgba8, or 4x 4k BC7)
struct AssetGeneratorImmediateParameters
{
    VkFence immFence{VK_NULL_HANDLE};
    VkCommandPool immCommandPool{VK_NULL_HANDLE};
    VkCommandBuffer immCommandBuffer{VK_NULL_HANDLE};

    OffsetAllocator::Allocator imageStagingAllocator{MODEL_GENERATION_STAGING_BUFFER_SIZE};
    AllocatedBuffer imageStagingBuffer{};
    AllocatedBuffer imageReceivingBuffer{};
};

enum class GenerateResponse
{
    UNABLE_TO_START = 0,
    STARTED,
    FINISHED
};

class AssetGenerator
{
public:
    AssetGenerator(VulkanContext* context, enki::TaskScheduler* taskscheduler);

    ~AssetGenerator();

    void WaitForAsyncModelGeneration() const;

    GenerateResponse GenerateWillModelAsync(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);

    GenerateResponse GenerateWillModel(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);

    const WillModelGenerationProgress& GetModelGenerationProgress() const { return modelGenerationProgress; }

    GenerateResponse GenerateKtxTexture(const std::filesystem::path& imageSource, const std::filesystem::path& outputPath, bool mipmapped);

private:
    struct GenerateTask : enki::ITaskSet
    {
        AssetGenerator* generator;
        std::filesystem::path gltfPath;
        std::filesystem::path outputPath;

        explicit GenerateTask(AssetGenerator* gen)
            : ITaskSet(1), generator(gen)
        {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
        {
            generator->GenerateWillModel_Internal(gltfPath, outputPath);
            generator->bIsGenerating.store(false, std::memory_order::release);
        }
    };

    RawGltfModel LoadGltf(const std::filesystem::path& source);

    bool WriteWillModel(RawGltfModel& rawModel, const std::filesystem::path& outputPath);

    void GenerateWillModel_Internal(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath);

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

    GenerateTask generateTask;

    std::atomic<bool> bIsGenerating{false};
    WillModelGenerationProgress modelGenerationProgress{};

    AssetGeneratorImmediateParameters immediateParameters;

private: // Cache
    std::vector<Node> sortedNodes;
    std::vector<bool> visited;
};

void WriteModelBinary(std::ofstream& file, const RawGltfModel& model);
} // Render

#endif //WILL_ENGINE_MODEL_GENERATOR_H
