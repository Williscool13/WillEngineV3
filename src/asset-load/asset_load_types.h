//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_TYPES_H
#define WILL_ENGINE_ASSET_LOAD_TYPES_H

#include <semaphore>

#include "core/containers/heap_array.h"
#include "core/memory/tlsf_allocator.h"
#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"
#include "core/memory/linear_allocator.h"
#include "core/types/math.h"
#include "engine/resources/material/material.h"
#include "engine/resources/model/model_types.h"


namespace Engine
{
struct PhysicsColliderAsset;
}

namespace AssetLoad
{
class CubemapLoadSlot;
class TextureLoadSlot;
class ProceduralTextureLoadSlot;
class StaticModelLoadSlot;
class ProceduralModelLoadSlot;
class PhysicsColliderLoadSlot;
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

struct BLASTransients
{
    VkDeviceSize blasScratchSize{};
    Core::HeapArray<VkAccelerationStructureGeometryKHR> geoms{};
    Core::HeapArray<uint32_t> primCounts{};
};

struct UnpackedStaticModel
{
    Core::HeapArray<Engine::Vertex> vertices{};
    Core::HeapArray<uint32_t> indices{};
    Core::HeapArray<uint32_t> meshletVertices{};
    Core::HeapArray<uint8_t> meshletTriangles{};
    Core::HeapArray<Meshlet> meshlets{};
    Core::HeapArray<Primitive> primitives{};
    Core::HeapArray<Engine::Material> materials{};
    Core::HeapArray<Engine::MeshInformation> allMeshes{};
    Core::HeapArray<Engine::Node> nodes{};
};

struct PhysicsCache
{
    Core::HeapArray<Vec3> positions;
    Core::HeapArray<uint32_t> indices;
};

using AudioSlotHandle = Core::Handle<AudioLoadSlot>;
using PipelineSlotHandle = Core::Handle<PipelineLoadSlot>;
using ModelSlotHandle = Core::Handle<StaticModelLoadSlot>;
using ProceduralModelSlotHandle = Core::Handle<ProceduralModelLoadSlot>;
using PhysicsColliderSlotHandle = Core::Handle<PhysicsColliderLoadSlot>;
using TextureSlotHandle = Core::Handle<TextureLoadSlot>;
using CubemapSlotHandle = Core::Handle<CubemapLoadSlot>;
using ProceduralTextureSlotHandle = Core::Handle<ProceduralTextureLoadSlot>;
using UploadStagingSlotHandle = Core::Handle<UploadStaging>;

struct AudioLoadRequest
{
    Audio::WillAudio* audioEntry;
};

struct AudioLoadComplete
{
    Audio::WillAudio* audioEntry;
    bool bSuccess;
};

struct PipelineLoadRequest
{
    Render::PipelineData* entry;
};

struct PipelineLoadComplete
{
    Render::PipelineData* pipelineData;
    bool bSuccess;
};

struct GPUDispatchRequest
{
    VkCommandBuffer cmd;
    VkFence fence;
    std::binary_semaphore* completionSignal;
};

struct StaticModelLoadRequest
{
    Engine::StaticModel* model;
};

struct StaticModelLoadComplete
{
    Engine::StaticModel* model;
    bool bSuccess;
};

struct PhysicsColliderLoadRequest
{
    Engine::PhysicsColliderAsset* collider;
};

struct PhysicsColliderLoadComplete
{
    Engine::PhysicsColliderAsset* collider;
    bool bSuccess;
};

struct TextureLoadRequest
{
    Engine::Texture* texture;
};

struct TextureLoadComplete
{
    Engine::Texture* texture;
    bool bSuccess;
};

struct CubemapLoadRequest
{
    Render::Cubemap* cubemap;
};

struct CubemapLoadComplete
{
    Render::Cubemap* cubemap;
    bool bSuccess;
};

struct SamplerLoadRequest
{
    Engine::Sampler* sampler;
};

struct SamplerLoadComplete
{
    Engine::Sampler* sampler;
    bool bSuccess;
};

struct ProceduralTextureLoadRequest
{
    Engine::Texture* texture;
    StringID pipelineId;
};

struct ProceduralTextureLoadComplete
{
    Engine::Texture* texture;
    bool bSuccess;
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_TYPES_H
