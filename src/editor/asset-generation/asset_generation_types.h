//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_GENERATION_TYPES_H
#define WILL_ENGINE_MODEL_GENERATION_TYPES_H

#include "core/containers/inline_string.h"
#include "core/containers/vector.h"
#include "engine/resources/material/material.h"
#include "engine/resources/model/model_types.h"
#include "engine/resources/sampler/sampler.h"

namespace Editor
{
constexpr uint32_t ASSET_GENERATOR_WORKER_COUNT = 16;
constexpr uint32_t MODEL_GENERATION_JOB_COUNT = 4;
constexpr uint32_t TEXTURE_GENERATION_JOB_COUNT = 4;
constexpr uint32_t ENVIRONMENT_MAP_GENERATION_JOB_COUNT = 1;
constexpr uint32_t TEXTURE_GENERATION_STAGING_BUFFER_SIZE = 64 * 1024 * 1024; // 64MB
constexpr uint32_t ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE = 128 * 1024 * 1024; // 128MB

// MEM std::array exception for constexpr array of names
static constexpr std::array<const char*, 32> ASSET_GENERATOR_WORKER_NAMES = {
    "AssetGenerator0", "AssetGenerator1", "AssetGenerator2", "AssetGenerator3",
    "AssetGenerator4", "AssetGenerator5", "AssetGenerator6", "AssetGenerator7",
    "AssetGenerator8", "AssetGenerator9", "AssetGenerator10", "AssetGenerator11",
    "AssetGenerator12", "AssetGenerator13", "AssetGenerator14", "AssetGenerator15",
    "AssetGenerator16", "AssetGenerator17", "AssetGenerator18", "AssetGenerator19",
    "AssetGenerator20", "AssetGenerator21", "AssetGenerator22", "AssetGenerator23",
    "AssetGenerator24", "AssetGenerator25", "AssetGenerator26", "AssetGenerator27",
    "AssetGenerator28", "AssetGenerator29", "AssetGenerator30", "AssetGenerator31",
};

struct RawImage
{
    int32_t w;
    int32_t h;
    int32_t bpp;
    Core::HeapArray<uint8_t> data;
};

struct RawStaticModel
{
    Core::InlineString<128> name{};

    // Only used when writing model, during read-time it's already embedded in materials
    Core::HeapArray<Engine::SamplerDesc> samplerInfos{};
    Core::HeapArray<RawImage> images{};

    // Vector instead of HeapArray because this accumulates over all submeshes in a gltf, so we can't preallocate for all at the beginning.
    Core::Vector<Vertex> vertices{};
    Core::Vector<uint32_t> indices{};
    Core::Vector<uint32_t> meshletVertices{};
    Core::Vector<uint8_t> meshletTriangles{};
    Core::Vector<Meshlet> meshlets{};

    Core::HeapArray<Primitive> primitives{};
    Core::HeapArray<Engine::Material> materials{};

    Core::HeapArray<Engine::MeshInformation> allMeshes{};
    Core::HeapArray<Engine::Node> nodes{};

    RawStaticModel() = default;

    RawStaticModel(const RawStaticModel&) = delete;

    RawStaticModel& operator=(const RawStaticModel&) = delete;

    RawStaticModel(RawStaticModel&&) noexcept = default;

    RawStaticModel& operator=(RawStaticModel&&) noexcept = default;

    void Init(Core::TlsfAllocator* alloc)
    {
        vertices = Core::Vector<Vertex>(alloc, Core::AllocTag::AssetModel);
        indices = Core::Vector<uint32_t>(alloc, Core::AllocTag::AssetModel);
        meshletVertices = Core::Vector<uint32_t>(alloc, Core::AllocTag::AssetModel);
        meshletTriangles = Core::Vector<uint8_t>(alloc, Core::AllocTag::AssetModel);
        meshlets = Core::Vector<Meshlet>(alloc, Core::AllocTag::AssetModel);
    }

    // Clears without freeing — slot reuses its allocated capacity across loads.
    void Reset()
    {
        vertices.Clear();
        indices.Clear();
        meshletVertices.Clear();
        meshletTriangles.Clear();
        meshlets.Clear();
    }
};

} // Render

#endif //WILL_ENGINE_MODEL_GENERATION_TYPES_H