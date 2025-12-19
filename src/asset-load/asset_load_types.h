//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_TYPES_H
#define WILL_ENGINE_ASSET_LOAD_TYPES_H
#include <cstdint>
#include <vector>

#include <ktx.h>
#include <volk.h>

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
constexpr uint32_t ASSET_LOAD_STAGING_BUFFER_SIZE = 2 * 64 * 1024 * 1024; // 2MB

struct UploadStaging
{
    Render::VulkanContext* context{};
    VkCommandBuffer commandBuffer{};
    VkFence fence{};

    Render::AllocatedBuffer stagingBuffer{};
    OffsetAllocator::Allocator stagingAllocator{ASSET_LOAD_STAGING_BUFFER_SIZE};

    UploadStaging() = default;
    ~UploadStaging() = default;
};


struct UnpackedWillModel
{
    std::string name{};
    bool bIsSkeletalModel{false};

    std::vector<VkSamplerCreateInfo> pendingSamplerInfos{};
    std::vector<ktxTexture2*> pendingTextures{};

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

        pendingSamplerInfos.clear();
        // destroy ktx texture2s
        // pendingTextures

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
