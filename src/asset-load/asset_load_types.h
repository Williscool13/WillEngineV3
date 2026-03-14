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
class ProceduralModelLoadSlot;
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


struct RawImage
{
    int32_t w;
    int32_t h;
    int32_t bpp;
    std::unique_ptr<uint8_t[]> imageData;
};

struct RawStaticModel
{
    std::string name{};

    // Only used when writing model, during read-time it's already embedded in materials
    std::vector<Engine::SamplerDesc> samplerInfos{};
    std::vector<RawImage> images{};

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};
    std::vector<uint32_t> meshletVertices{};
    std::vector<uint8_t> meshletTriangles{};
    std::vector<Meshlet> meshlets{};

    std::vector<Primitive> primitives{};
    std::vector<Engine::Material> materials{};

    std::vector<Engine::MeshInformation> allMeshes{};
    std::vector<Engine::Node> nodes{};


    RawStaticModel() = default;

    RawStaticModel(const RawStaticModel&) = delete;

    RawStaticModel& operator=(const RawStaticModel&) = delete;

    RawStaticModel(RawStaticModel&&) noexcept = default;

    RawStaticModel& operator=(RawStaticModel&&) noexcept = default;

    void Reset()
    {
        vertices.clear();
        indices.clear();
        meshletVertices.clear();
        meshletTriangles.clear();
        meshlets.clear();
        primitives.clear();
        materials.clear();
        allMeshes.clear();
        nodes.clear();
    }
};

using AudioSlotHandle = Core::Handle<AudioLoadSlot>;
using PipelineSlotHandle = Core::Handle<PipelineLoadSlot>;
using ModelSlotHandle = Core::Handle<StaticModelLoadSlot>;
using ProceduralModelSlotHandle = Core::Handle<ProceduralModelLoadSlot>;
using TextureSlotHandle = Core::Handle<TextureLoadSlot>;
using CubemapSlotHandle = Core::Handle<CubemapLoadSlot>;
using UploadStagingSlotHandle = Core::Handle<UploadStaging>;
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_TYPES_H
