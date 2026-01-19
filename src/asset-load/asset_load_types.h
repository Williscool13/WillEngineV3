//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_TYPES_H
#define WILL_ENGINE_ASSET_LOAD_TYPES_H

#include <vector>

#include "render/model/model_types.h"
#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"
#include "../render/resource_manager.h"
#include "core/allocators/linear_allocator.h"
#include "engine/asset_manager_types.h"
#include "render/model/will_model_asset.h"


namespace Render
{
struct PipelineEntry;
struct WillModel;
struct VulkanContext;
}

namespace AssetLoad
{
class UploadStaging
{
public:
    UploadStaging(Render::VulkanContext* context, VkCommandBuffer commandBuffer, uint32_t stagingSize);

    ~UploadStaging();

    void StartCommandBuffer();

    void SubmitCommandBuffer();

    [[nodiscard]] bool IsReady() const;

    void WaitForFence() const;

    [[nodiscard]] bool IsCommandBufferStarted() const { return bCommandBufferStarted; }

    VkCommandBuffer GetCommandBuffer() const { return commandBuffer; }
    Core::LinearAllocator& GetStagingAllocator() { return stagingAllocator; }
    Render::AllocatedBuffer& GetStagingBuffer() { return stagingBuffer; }

private:
    Render::VulkanContext* context{};
    VkCommandBuffer commandBuffer{};
    VkFence fence{};

    Render::AllocatedBuffer stagingBuffer{};
    Core::LinearAllocator stagingAllocator{0};

    // Transient
    bool bCommandBufferStarted = false;
};


struct UnpackedWillModel
{
    std::string name{};
    bool bIsSkeletalModel{false};

    std::vector<SkinnedVertex> vertices{};
    std::vector<uint32_t> meshletVertices{};
    std::vector<uint8_t> meshletTriangles{};
    std::vector<Meshlet> meshlets{};

    std::vector<MeshletPrimitive> primitives{};
    std::vector<MaterialProperties> materials{};

    std::vector<Render::MeshInformation> allMeshes{};
    std::vector<Render::Node> nodes{};

    std::vector<Render::Animation> animations;
    std::vector<glm::mat4> inverseBindMatrices{};

    UnpackedWillModel() = default;

    UnpackedWillModel(const UnpackedWillModel&) = delete;

    UnpackedWillModel& operator=(const UnpackedWillModel&) = delete;

    UnpackedWillModel(UnpackedWillModel&&) noexcept = default;

    UnpackedWillModel& operator=(UnpackedWillModel&&) noexcept = default;

    void Reset()
    {
        name.clear();
        bIsSkeletalModel = false;

        vertices.clear();
        meshletVertices.clear();
        meshletTriangles.clear();
        meshlets.clear();
        primitives.clear();
        materials.clear();
        allMeshes.clear();
        animations.clear();
        inverseBindMatrices.clear();
    }
};


struct WillModelLoadRequest
{
    Engine::WillModelHandle willModelHandle;
    Render::WillModel* model;
};

struct WillModelComplete
{
    Engine::WillModelHandle willModelHandle;
    Render::WillModel* model;
    bool bSuccess;
};

struct TextureLoadRequest
{
    Engine::TextureHandle textureHandle;
    Render::Texture* texture;
};

struct TextureComplete
{
    Engine::TextureHandle textureHandle;
    Render::Texture* texture;
    bool success;
};

struct PipelineLoadRequest
{
    std::string name;
    Render::PipelineEntry* entry;
};

struct PipelineComplete
{
    std::string name;
    Render::PipelineEntry* entry;
    bool success;
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_TYPES_H
