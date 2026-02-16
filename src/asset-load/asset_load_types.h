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


namespace AssetLoad
{
class CubemapLoadSlot;
class TextureLoadSlot;
class WillModelLoadSlot;
class AudioLoadSlot;
class PipelineLoadSlot;
}

namespace Render
{
struct PipelineData;
struct WillModel;
struct VulkanContext;
}

namespace AssetLoad
{

class UploadStaging
{
public:
    UploadStaging();

    ~UploadStaging();

    void Initialize(Render::VulkanContext* context, size_t stagingSize);

    Core::LinearAllocator& GetStagingAllocator() { return stagingAllocator; }
    Render::AllocatedBuffer& GetStagingBuffer() { return stagingBuffer; }

private:
    bool bStagingExists{false};
    Render::AllocatedBuffer stagingBuffer{};
    Core::LinearAllocator stagingAllocator{1};
};


struct UnpackedWillModel
{
    std::vector<Vertex> vertices{};
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

using AudioSlotHandle = Core::Handle<AudioLoadSlot>;
using PipelineSlotHandle = Core::Handle<PipelineLoadSlot>;
using ModelSlotHandle = Core::Handle<WillModelLoadSlot>;
using TextureSlotHandle = Core::Handle<TextureLoadSlot>;
using CubemapSlotHandle = Core::Handle<CubemapLoadSlot>;
using UploadStagingSlotHandle = Core::Handle<UploadStaging>;
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_TYPES_H
