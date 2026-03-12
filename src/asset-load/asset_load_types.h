//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_TYPES_H
#define WILL_ENGINE_ASSET_LOAD_TYPES_H

#include <vector>

#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"
#include "core/allocators/linear_allocator.h"
#include "../engine/resources/material/material.h"
#include "engine/resources/model/model_types.h"


namespace AssetLoad
{
class CubemapLoadSlot;
class TextureLoadSlot;
class StaticModelLoadSlot;
class AudioLoadSlot;
class PipelineLoadSlot;
}

namespace Render
{
struct PipelineData;
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


struct UnpackedStaticModel
{
    std::vector<Vertex> vertices{};
    std::vector<uint32_t> meshletVertices{};
    std::vector<uint8_t> meshletTriangles{};
    std::vector<Meshlet> meshlets{};

    std::vector<MeshletPrimitive> primitives{};
    std::vector<Engine::Material> materials{};

    std::vector<Engine::MeshInformation> allMeshes{};
    std::vector<Engine::Node> nodes{};

    std::vector<Engine::Animation> animations;
    std::vector<glm::mat4> inverseBindMatrices{};

    UnpackedStaticModel() = default;

    UnpackedStaticModel(const UnpackedStaticModel&) = delete;

    UnpackedStaticModel& operator=(const UnpackedStaticModel&) = delete;

    UnpackedStaticModel(UnpackedStaticModel&&) noexcept = default;

    UnpackedStaticModel& operator=(UnpackedStaticModel&&) noexcept = default;

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
using ModelSlotHandle = Core::Handle<StaticModelLoadSlot>;
using TextureSlotHandle = Core::Handle<TextureLoadSlot>;
using CubemapSlotHandle = Core::Handle<CubemapLoadSlot>;
using UploadStagingSlotHandle = Core::Handle<UploadStaging>;
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_TYPES_H
