//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_TYPES_H
#define WILL_ENGINE_ASSET_LOAD_TYPES_H

#include <vector>

#include "core/containers/vector.h"
#include "core/memory/tlsf_allocator.h"
#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"
#include "core/memory/linear_allocator.h"
#include "engine/resources/material/material.h"
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

    // Per-slot arena: Init() sets allocator once; Reset() clears without freeing (reuses capacity).
    Core::Vector<Vertex> vertices{};
    Core::Vector<uint32_t> indices{};
    Core::Vector<uint32_t> meshletVertices{};
    Core::Vector<uint8_t> meshletTriangles{};
    Core::Vector<Meshlet> meshlets{};

    Core::Vector<Primitive> primitives{};
    Core::Vector<Engine::Material> materials{};

    Core::Vector<Engine::MeshInformation> allMeshes{};
    Core::Vector<Engine::Node> nodes{};

    RawStaticModel() = default;

    RawStaticModel(const RawStaticModel&) = delete;

    RawStaticModel& operator=(const RawStaticModel&) = delete;

    RawStaticModel(RawStaticModel&&) noexcept = default;

    RawStaticModel& operator=(RawStaticModel&&) noexcept = default;

    void Init(Core::TlsfAllocator* alloc)
    {
        vertices         = Core::Vector<Vertex>(alloc, Core::AllocTag::AssetModel);
        indices          = Core::Vector<uint32_t>(alloc, Core::AllocTag::AssetModel);
        meshletVertices  = Core::Vector<uint32_t>(alloc, Core::AllocTag::AssetModel);
        meshletTriangles = Core::Vector<uint8_t>(alloc, Core::AllocTag::AssetModel);
        meshlets         = Core::Vector<Meshlet>(alloc, Core::AllocTag::AssetModel);
        primitives       = Core::Vector<Primitive>(alloc, Core::AllocTag::AssetModel);
        materials        = Core::Vector<Engine::Material>(alloc, Core::AllocTag::AssetModel);
        allMeshes        = Core::Vector<Engine::MeshInformation>(alloc, Core::AllocTag::AssetModel);
        nodes            = Core::Vector<Engine::Node>(alloc, Core::AllocTag::AssetModel);
    }

    // Clears without freeing — slot reuses its allocated capacity across loads.
    void Reset()
    {
        vertices.Clear();
        indices.Clear();
        meshletVertices.Clear();
        meshletTriangles.Clear();
        meshlets.Clear();
        primitives.Clear();
        materials.Clear();
        allMeshes.Clear();
        nodes.Clear();
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
