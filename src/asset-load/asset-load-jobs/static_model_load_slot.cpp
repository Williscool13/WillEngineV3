//
// Created by William on 2025-12-23.
//

#include "static_model_load_slot.h"

#include <fstream>
#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "asset-load/asset_load_utils.h"
#include "core/containers/fixed_vector.h"
#include "core/containers/heap_array.h"
#include "core/memory/memory_manager.h"
#include "meshoptimizer/src/meshoptimizer.h"
#include "engine/compression/compression.h"
#include "engine/resources/model/model_format.h"
#include "engine/resources/model/static_model.h"
#include "engine/serialization/serialization.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
StaticModelLoadSlot::StaticModelLoadSlot() = default;

StaticModelLoadSlot::~StaticModelLoadSlot() = default;

void StaticModelLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);
}

void StaticModelLoadSlot::Launch(
    ModelSlotHandle _modelSlotHandle,
    UploadStagingSlotHandle _uploadStagingSlotHandle,
    UploadStaging* _uploadStaging,
    Engine::StaticModel* _outputModel)
{
    modelSlotHandle = _modelSlotHandle;
    uploadStagingSlotHandle = _uploadStagingSlotHandle;
    uploadStaging = _uploadStaging;
    outputModel = _outputModel;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void StaticModelLoadSlot::Clear()
{
    modelSlotHandle = {};
    uploadStagingSlotHandle = {};
    outputModel = nullptr;
    uploadStaging = nullptr;

    // RAII dealloc
    rawData = {};
}

void StaticModelLoadSlot::LoadModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadModelFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->modelSlotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->modelSlotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    // Cannot fail past those first 2 checks
    loadSlot->PrepareUploadData();

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(loadSlot->context->transferQueueFamily);
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(loadSlot->context->device, &poolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, commandPool);
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(loadSlot->context->device, &cmdInfo, &cmd));

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(loadSlot->context->device, &fenceInfo, nullptr, &fence));

    auto submitAndWait = [&](bool reset) {
        ZoneScopedN("SubmitAndWait");

        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_requestDispatchCallback(cmd, fence, &done);
        done.acquire();

        if (reset) {
            VK_CHECK(vkResetFences(loadSlot->context->device, 1, &fence));
            VK_CHECK(vkResetCommandBuffer(cmd, 0));

            VkCommandBufferBeginInfo beginInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
        }
    };

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    loadSlot->UploadGeometry(cmd, submitAndWait);

    VK_CHECK(vkEndCommandBuffer(cmd));
    std::binary_semaphore done(0);
    loadSlot->_requestDispatchCallback(cmd, fence, &done);
    done.acquire();

    vkDestroyFence(loadSlot->context->device, fence, nullptr);
    vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);

    loadSlot->_notifyCallback(true, loadSlot->modelSlotHandle, loadSlot->uploadStagingSlotHandle);
}

bool StaticModelLoadSlot::LoadModelFromDisk()
{
    ZoneScopedN("LoadModelFromDisk");

    if (!outputModel->source.Exists()) {
        SPDLOG_ERROR("Failed to find path to static model - {}", outputModel->name.c_str());
        return false;
    }

    Engine::WStaticModelHeader header{};
    Core::HeapArray<uint8_t> body;
    Core::HeapArray<uint8_t> nodeData;

    //
    {
        ZoneScopedN("ReadFile");
        std::ifstream file(outputModel->source.c_str(), std::ios::binary);
        if (!file) {
            SPDLOG_ERROR("Failed to open static model - {}", outputModel->name.c_str());
            return false;
        }
        auto optHeader = Engine::ReadWStaticModelHeader(file);
        if (!optHeader) {
            SPDLOG_ERROR("Failed to read static model header - {}", outputModel->name.c_str());
            return false;
        }
        header = *optHeader;

        Core::HeapArray<uint8_t> compressedBody(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, header.compressedBodySize);
        file.seekg(static_cast<std::streamoff>(header.dataOffset));
        file.read(reinterpret_cast<char*>(compressedBody.Data()), static_cast<std::streamsize>(header.compressedBodySize));

        body = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, header.uncompressedBodySize);
        Engine::DecompressLZ4(compressedBody.Data(), header.compressedBodySize, body.Data(), header.uncompressedBodySize);
        compressedBody.Reset();

        file.seekg(0, std::ios::end);
        const size_t fileSize = file.tellg();
        const size_t nodeDataStart = header.dataOffset + header.compressedBodySize;

        size_t nodesSize = fileSize - nodeDataStart;
        nodeData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, nodesSize);
        file.seekg(static_cast<std::streamoff>(nodeDataStart));
        file.read(reinterpret_cast<char*>(nodeData.Data()), static_cast<std::streamsize>(nodesSize));
    }

    //
    {
        ZoneScopedN("ParseGeometryData");
        auto readArray = [&]<typename T>(Core::HeapArray<T>& vec, uint32_t offset, uint32_t count) {
            if (count > 0) {
                assert(!vec.IsAllocated() && "Array already allocated (memory leak)");
                vec = Core::HeapArray<T>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, count);
                memcpy(vec.Data(), body.Data() + offset, count * sizeof(T));
            }
        };

        readArray(rawData.vertices, header.vertexOffset, header.vertexCount);
        readArray(rawData.indices, header.indexOffset, header.indexCount);
        readArray(rawData.meshletVertices, header.meshletVertexOffset, header.meshletVertexCount);
        readArray(rawData.meshletTriangles, header.meshletTriangleOffset, header.meshletTriangleCount);
        readArray(rawData.meshlets, header.meshletOffset, header.meshletCount);
        readArray(rawData.primitives, header.primitiveOffset, header.primitiveCount);
    }

    //
    {
        ZoneScopedN("ParseMaterials");
        if (header.materialCount > 0) {
            const uint8_t* ptr = body.Data() + header.materialOffset;
            assert(!rawData.materials.IsAllocated() && "Vector already allocated (memory leak)");
            rawData.materials = Core::HeapArray<Engine::Material>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, header.materialCount);
            for (uint32_t i = 0; i < header.materialCount; ++i) {
                Engine::ReadMaterial(ptr, rawData.materials[i]);
            }
        }
    }

    //
    {
        ZoneScopedN("ParseMeshes");
        if (header.meshCount > 0) {
            const uint8_t* ptr = body.Data() + header.meshOffset;
            assert(!rawData.allMeshes.IsAllocated() && "Vector already allocated (memory leak)");
            rawData.allMeshes = Core::HeapArray<Engine::MeshInformation>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, header.meshCount);
            for (uint32_t i = 0; i < header.meshCount; ++i) {
                Engine::ReadMeshInformation(ptr, rawData.allMeshes[i]);
            }
        }
    }

    //
    {
        ZoneScopedN("ParseNodes");
        if (header.nodeCount > 0) {
            const uint8_t* ptr = nodeData.Data();
            assert(!rawData.nodes.IsAllocated() && "Vector already allocated (memory leak)");
            rawData.nodes = Core::HeapArray<Engine::Node>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, header.nodeCount);

            for (uint32_t i = 0; i < header.nodeCount; ++i) {
                Engine::ReadNode(ptr, rawData.nodes[i]);
            }

            // The loop advances the ptr. This gets how many bytes we advanced by
            const size_t bytesConsumed = ptr - nodeData.Data();
            const size_t remaining = nodeData.Size() - bytesConsumed;

            if (remaining >= sizeof(Engine::ModelBounds)) {
                memcpy(&outputModel->bounds, ptr, sizeof(Engine::ModelBounds));
            }
        }
    }

    return true;
}

bool StaticModelLoadSlot::AllocateGPUResources() const
{
    struct BufferAlloc
    {
        std::mutex* mutex;
        OffsetAllocator::Allocator* allocator;
        OffsetAllocator::Allocation* allocation;
    };

    Core::InlineVector<BufferAlloc, 5> allocated;

    auto rollback = [&]() {
        for (auto& prev : allocated) {
            std::lock_guard lock(*prev.mutex);
            prev.allocator->free(*prev.allocation);
        }
    };

    auto tryAlloc = [&](std::mutex& m, OffsetAllocator::Allocator& a, OffsetAllocator::Allocation& out, size_t size, const char* name) -> bool {
        std::lock_guard lock(m);
        out = a.allocate(size);
        if (out.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            SPDLOG_ERROR("[StaticModelLoadSlot] Not enough space in {}", name);
            return false;
        }
        allocated.PushBack({&m, &a, &out});
        return true;
    };

    if (!tryAlloc(resourceManager->vertexBufferAllocatorMutex, resourceManager->vertexBufferAllocator,
                  outputModel->modelData.vertexAllocation, rawData.vertices.Size() * sizeof(Vertex),
                  "mega vertex buffer")) {
        rollback();
        return false;
    }

    if (!tryAlloc(resourceManager->meshletVertexBufferAllocatorMutex, resourceManager->meshletVertexBufferAllocator,
                  outputModel->modelData.meshletVertexAllocation, rawData.meshletVertices.Size() * sizeof(uint32_t),
                  "mega meshlet vertex buffer")) {
        rollback();
        return false;
    }

    if (!tryAlloc(resourceManager->meshletTriangleBufferAllocatorMutex, resourceManager->meshletTriangleBufferAllocator,
                  outputModel->modelData.meshletTriangleAllocation, rawData.meshletTriangles.Size() / 3 * sizeof(uint32_t),
                  "mega meshlet triangle buffer")) {
        rollback();
        return false;
    }

    if (!tryAlloc(resourceManager->meshletBufferAllocatorMutex, resourceManager->meshletBufferAllocator,
                  outputModel->modelData.meshletAllocation, rawData.meshlets.Size() * sizeof(Meshlet),
                  "mega meshlet buffer")) {
        rollback();
        return false;
    }

    if (!tryAlloc(resourceManager->primitiveBufferAllocatorMutex, resourceManager->primitiveBufferAllocator,
                  outputModel->modelData.primitiveAllocation, rawData.primitives.Size() * sizeof(Primitive),
                  "mega primitive buffer")) {
        rollback();
        return false;
    }

    return true;
}

void StaticModelLoadSlot::PrepareUploadData()
{
    // Adjust offsets
    uint32_t vertexOffset = outputModel->modelData.vertexAllocation.offset / sizeof(Vertex);
    uint32_t meshletVerticesOffset = outputModel->modelData.meshletVertexAllocation.offset / sizeof(uint32_t);
    uint32_t meshletTriangleOffset = outputModel->modelData.meshletTriangleAllocation.offset / sizeof(uint32_t);

    for (Meshlet& meshlet : rawData.meshlets) {
        meshlet.vertexOffset += vertexOffset;
        meshlet.meshletVertexOffset += meshletVerticesOffset;
        meshlet.meshletTriangleOffset = meshlet.meshletTriangleOffset / 3 + meshletTriangleOffset;
    }

    uint32_t meshletOffset = outputModel->modelData.meshletAllocation.offset / sizeof(Meshlet);
    for (auto& primitive : rawData.primitives) {
        primitive.meshletOffset += meshletOffset;
    }

    uint32_t primitiveOffsetCount = outputModel->modelData.primitiveAllocation.offset / sizeof(Primitive);
    for (auto& mesh : rawData.allMeshes) {
        for (auto& primitiveIndex : mesh.primitiveProperties) {
            primitiveIndex.index += primitiveOffsetCount;
        }
    }

    //
    {
        outputModel->physicsCache = PhysicsCache{};
        auto& physicsCache = outputModel->physicsCache.value();
        physicsCache.positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.vertices.Size());
        for (size_t i = 0; i < rawData.vertices.Size(); ++i) {
            // todo fix..?
            //physicsCache.positions[i] = rawData.vertices[i].position;
        }

        physicsCache.indices = Core::HeapArray<uint32_t>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.indices.Size());
        for (size_t i = 0; i < rawData.indices.Size(); ++i) {
            physicsCache.indices[i] = rawData.indices[i];
        }

        // If no bounds from model generation, compute them here (strictly worse)
        // Compute with full fidelity index buffer
        if (outputModel->bounds.sphere.radius == 0.f) {
            outputModel->bounds = ComputeBounds(physicsCache.positions);
        }

        // todo parameterize
        constexpr size_t kSimplifyThreshold = 1500;
        constexpr size_t kSimplifyFloor = 1500;
        constexpr float kSimplifyRatio = 0.15f;
        constexpr float kSimplifyError = 0.01f;
        if (physicsCache.indices.Size() > kSimplifyThreshold) {
            const size_t target = std::max(kSimplifyFloor, static_cast<size_t>(physicsCache.indices.Size() * kSimplifyRatio));
            Core::HeapArray<uint32_t> simplified = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, physicsCache.indices.Size());
            const size_t result = meshopt_simplify(
                simplified.Data(),
                physicsCache.indices.Data(), physicsCache.indices.Size(),
                &physicsCache.positions[0].x, physicsCache.positions.Size(), sizeof(Vec3),
                target, kSimplifyError
            );

            physicsCache.indices = Core::HeapArray<uint32_t>(&memoryManager->Assets(), Core::AllocTag::AssetModel, result);
            for (size_t i = 0; i < result; ++i) {
                physicsCache.indices[i] = simplified[i];
            }
        }
    }

    //
    {
        Core::HeapArray<Engine::MeshInformation>& dst = outputModel->modelData.meshes;
        assert(!dst.IsAllocated() && "modelData.meshes was found to be allocated (memory leak)");
        dst = Core::HeapArray<Engine::MeshInformation>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.allMeshes.Size());
        for (size_t i = 0; i < rawData.allMeshes.Size(); ++i) {
            dst[i] = rawData.allMeshes[i];
        }
    }

    //
    {
        if (rawData.nodes.IsAllocated() && !rawData.nodes.IsEmpty()) {
            Core::HeapArray<Engine::Node>& dst = outputModel->modelData.nodes;
            assert(!dst.IsAllocated() && "modelData.nodes was found to be allocated (memory leak)");
            dst = Core::HeapArray<Engine::Node>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.nodes.Size());
            for (size_t i = 0; i < rawData.nodes.Size(); ++i) {
                dst[i] = rawData.nodes[i];
            }
        }
    }


    if (rawData.materials.IsAllocated() && !rawData.materials.IsEmpty()) {
        Core::HeapArray<Engine::Material>& dst = outputModel->modelData.materials;
        assert(!dst.IsAllocated() && "modelData.materials was found to be allocated (memory leak)");
        dst = Core::HeapArray<Engine::Material>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.materials.Size());
        for (size_t i = 0; i < rawData.materials.Size(); ++i) {
            dst[i] = rawData.materials[i];
        }
    }
}

void StaticModelLoadSlot::UploadGeometry(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadGeometry");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    auto uploadBuffer = [&](const void* sourceData, size_t count, size_t elementSize,
                            VkBuffer targetBuffer, VkDeviceSize targetOffset) {
        size_t totalSize = count * elementSize;
        size_t uploaded = 0;

        while (uploaded < totalSize) {
            size_t remaining = totalSize - uploaded;
            size_t allocation = stagingAllocator.Allocate(remaining);

            if (allocation == SIZE_MAX) {
                size_t freeSpace = stagingAllocator.GetRemaining();
                if (freeSpace == 0) {
                    submitAndWait(true);
                    stagingAllocator.Reset();
                    continue;
                }
                remaining = std::min(remaining, freeSpace);
                allocation = stagingAllocator.Allocate(remaining);
                assert(allocation != SIZE_MAX);
            }

            const char* srcPtr = static_cast<const char*>(sourceData) + uploaded;
            char* dstPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
            memcpy(dstPtr, srcPtr, remaining);

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = allocation;
            copyRegion.dstOffset = targetOffset + uploaded;
            copyRegion.size = remaining;
            vkCmdCopyBuffer(cmd, stagingBuffer.handle, targetBuffer, 1, &copyRegion);

            uploaded += remaining;
        }
    };

    uploadBuffer(rawData.vertices.Data(), rawData.vertices.Size(), sizeof(Vertex),
                 resourceManager->megaVertexBuffer.handle, outputModel->modelData.vertexAllocation.offset);

    uploadBuffer(rawData.meshletVertices.Data(), rawData.meshletVertices.Size(), sizeof(uint32_t),
                 resourceManager->megaMeshletVerticesBuffer.handle, outputModel->modelData.meshletVertexAllocation.offset);

    // Pack triangles
    Core::HeapArray<uint32_t> packedTriangles(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, rawData.meshletTriangles.Size() / 3);
    for (size_t i = 0; i < rawData.meshletTriangles.Size(); i += 3) {
        uint32_t packed = rawData.meshletTriangles[i + 0] |
                          (rawData.meshletTriangles[i + 1] << 8) |
                          (rawData.meshletTriangles[i + 2] << 16);
        packedTriangles[i / 3] = packed;
    }

    uploadBuffer(packedTriangles.Data(), packedTriangles.Size(), sizeof(uint32_t),
                 resourceManager->megaMeshletTrianglesBuffer.handle, outputModel->modelData.meshletTriangleAllocation.offset);

    uploadBuffer(rawData.meshlets.Data(), rawData.meshlets.Size(), sizeof(Meshlet),
                 resourceManager->megaMeshletBuffer.handle, outputModel->modelData.meshletAllocation.offset);

    uploadBuffer(rawData.primitives.Data(), rawData.primitives.Size(), sizeof(Primitive),
                 resourceManager->primitiveBuffer.handle, outputModel->modelData.primitiveAllocation.offset);

    // Queue family transfer barriers
    VkBufferMemoryBarrier2 releaseBarriers[5];
    uint32_t barrierCount = 0;

    auto createBufferBarrier = [&](VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
        VkBufferMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .srcQueueFamilyIndex = context->transferQueueFamily,
            .dstQueueFamilyIndex = context->graphicsQueueFamily,
            .buffer = buffer,
            .offset = offset,
            .size = size
        };
        if (context->bMaintenance9Enabled) {
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        }
        return barrier;
    };

    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaVertexBuffer.handle,
                                                          outputModel->modelData.vertexAllocation.offset, rawData.vertices.Size() * sizeof(Vertex));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaMeshletVerticesBuffer.handle,
                                                          outputModel->modelData.meshletVertexAllocation.offset, rawData.meshletVertices.Size() * sizeof(uint32_t));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaMeshletTrianglesBuffer.handle,
                                                          outputModel->modelData.meshletTriangleAllocation.offset, rawData.meshletTriangles.Size() / 3 * sizeof(uint32_t));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaMeshletBuffer.handle,
                                                          outputModel->modelData.meshletAllocation.offset, rawData.meshlets.Size() * sizeof(Meshlet));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->primitiveBuffer.handle,
                                                          outputModel->modelData.primitiveAllocation.offset, rawData.primitives.Size() * sizeof(Primitive));

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.bufferMemoryBarrierCount = barrierCount;
    depInfo.pBufferMemoryBarriers = releaseBarriers;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    for (uint32_t i = 0; i < barrierCount; ++i) {
        outputModel->bufferAcquireOps.PushBack(Render::VkHelpers::FromVkBarrier(releaseBarriers[i]));
    }
}
} // AssetLoad
