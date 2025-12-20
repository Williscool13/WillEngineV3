//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_TYPES_H
#define WILL_ENGINE_ASSET_LOAD_TYPES_H

#include <vector>

#include "offsetAllocator.hpp"
#include "render/model/model_types.h"
#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"
#include "render/vulkan/vk_resource_manager.h"


namespace Render
{
struct VulkanContext;
}

namespace AssetLoad
{
constexpr uint32_t ASSET_LOAD_STAGING_BUFFER_SIZE = 32 * 1024 * 1024; // 32MB

class UploadStaging
{
public:
    UploadStaging() = default;

    ~UploadStaging();

    void Initialize(Render::VulkanContext* _context, VkCommandBuffer _commandBuffer);

    void StartCommandBuffer();

    void SubmitCommandBuffer();

    [[nodiscard]] bool IsReady() const;

    [[nodiscard]] bool IsCommandBufferStarted() const { return bCommandBufferStarted; }

    VkCommandBuffer GetCommandBuffer() const { return commandBuffer; }
    OffsetAllocator::Allocator& GetStagingAllocator() { return stagingAllocator; }
    Render::AllocatedBuffer& GetStagingBuffer() { return stagingBuffer; }

private:
    Render::VulkanContext* context{};
    VkCommandBuffer commandBuffer{};
    VkFence fence{};

    Render::AllocatedBuffer stagingBuffer{};
    OffsetAllocator::Allocator stagingAllocator{ASSET_LOAD_STAGING_BUFFER_SIZE};

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
    std::vector<uint32_t> nodeRemap{};

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
        nodes.clear();
        nodeRemap.clear();
        animations.clear();
        inverseBindMatrices.clear();
    }
};


struct WillModelLoadRequest
{
    Render::WillModelHandle willModelHandle;
};

struct WillModelComplete
{
    Render::WillModelHandle willModelHandle;
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_TYPES_H
