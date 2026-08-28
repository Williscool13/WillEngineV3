//
// Created by William on 2025-12-23.
//

#include "static_model_load_slot.h"

#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "asset-load/asset_load_utils.h"
#include "platform/file_utils.h"
#include "core/containers/fixed_vector.h"
#include "core/containers/heap_array.h"
#include "core/memory/memory_manager.h"
#include "meshoptimizer/src/meshoptimizer.h"
#include "engine/compression/compression.h"
#include "engine/resources/model/model_format.h"
#include "engine/resources/model/static_model.h"
#include "engine/serialization/serialization.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
StaticModelLoadSlot::StaticModelLoadSlot() = default;

StaticModelLoadSlot::~StaticModelLoadSlot()
{
    if (uploadCompleteSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(context->device, uploadCompleteSemaphore, context->HostAllocCallbacks());
    }
    transferSubmit.Destroy(context);
    graphicsSubmit.Destroy(context);
}

void StaticModelLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore signalSemaphore)> transferDispatchCallback,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore waitSemaphore)> graphicsDispatchCallback,
    Core::InlineFunction<void(bool success, ModelSlotHandle modelSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestTransferDispatchCallback = std::move(transferDispatchCallback);
    _requestGraphicsDispatchCallback = std::move(graphicsDispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    VkSemaphoreCreateInfo semaphoreInfo = Render::VkHelpers::SemaphoreCreateInfo();
    VK_CHECK(vkCreateSemaphore(context->device, &semaphoreInfo, context->HostAllocCallbacks(), &uploadCompleteSemaphore));

    transferSubmit.Initialize(context, context->transferQueueFamily);
    graphicsSubmit.Initialize(context, context->graphicsQueueFamily);
}

void StaticModelLoadSlot::Launch(
    ModelSlotHandle _modelSlotHandle,
    UploadStaging* _uploadStaging,
    Engine::StaticModel* _outputModel)
{
    modelSlotHandle = _modelSlotHandle;
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
    outputModel = nullptr;
    uploadStaging = nullptr;

    // RAII dealloc
    rawData = {};
    blasTransients = {};
}

void StaticModelLoadSlot::LoadModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadModelFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->modelSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->modelSlotHandle);
        loadSlot->Clear();
        return;
    }

    // Cannot fail past those first 2 checks
    loadSlot->PrepareUploadData();

    VkCommandBuffer cmd = loadSlot->transferSubmit.cmd;
    VkFence fence = loadSlot->transferSubmit.fence;
    VkSemaphore uploadCompleteSemaphore = loadSlot->uploadCompleteSemaphore;

    auto submitAndWait = [&](bool reset) {
        ZoneScopedN("SubmitAndWait");

        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_requestTransferDispatchCallback(cmd, fence, &done, VK_NULL_HANDLE);
        done.acquire();
        VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

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
    loadSlot->_requestTransferDispatchCallback(cmd, fence, &done, uploadCompleteSemaphore);
    done.acquire();
    VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

    loadSlot->transferSubmit.Reset(loadSlot->context);

    // Build BLAS on graphics queue
    {
        VkCommandBuffer graphicsCmd = loadSlot->graphicsSubmit.cmd;
        VkFence graphicsFence = loadSlot->graphicsSubmit.fence;

        VkCommandBufferBeginInfo graphicsBeginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &graphicsBeginInfo));

        VkSemaphore waitSemaphore = uploadCompleteSemaphore;
        auto graphicsSubmitAndWait = [&](bool reset) {
            ZoneScopedN("GraphicsSubmitAndWait");
            VK_CHECK(vkEndCommandBuffer(graphicsCmd));
            std::binary_semaphore done(0);
            loadSlot->_requestGraphicsDispatchCallback(graphicsCmd, graphicsFence, &done, waitSemaphore);
            waitSemaphore = VK_NULL_HANDLE;
            done.acquire();
            VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &graphicsFence, VK_TRUE, UINT64_MAX));
            if (reset) {
                VK_CHECK(vkResetFences(loadSlot->context->device, 1, &graphicsFence));
                VK_CHECK(vkResetCommandBuffer(graphicsCmd, 0));
                VkCommandBufferBeginInfo restartInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &restartInfo));
            }
        };

        const bool bBlasBuilt = loadSlot->BuildBLAS(graphicsCmd, graphicsSubmitAndWait);

        loadSlot->graphicsSubmit.Reset(loadSlot->context);

        loadSlot->_notifyCallback(bBlasBuilt, loadSlot->modelSlotHandle);
    }
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
        Platform::ScopedFileMapping map(outputModel->source, true);
        if (!map.data) {
            SPDLOG_ERROR("Failed to open static model - {}", outputModel->name.c_str());
            return false;
        }
        auto optHeader = Engine::ReadWStaticModelHeader(map.data, map.size);
        if (!optHeader) {
            SPDLOG_ERROR("Failed to read static model header - {}", outputModel->name.c_str());
            return false;
        }
        header = *optHeader;

        if (header.dataOffset + header.compressedBodySize > map.size) {
            SPDLOG_ERROR("Static model body out of range - {}", outputModel->name.c_str());
            return false;
        }

        body = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, header.uncompressedBodySize);
        Engine::Decompress(header.compressionType, map.data + header.dataOffset, header.compressedBodySize, body.Data(), header.uncompressedBodySize);

        const size_t nodeDataStart = header.dataOffset + header.compressedBodySize;
        const size_t nodesSize = map.size - nodeDataStart;
        nodeData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, nodesSize);
        memcpy(nodeData.Data(), map.data + nodeDataStart, nodesSize);
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

bool StaticModelLoadSlot::AllocateGPUResources()
{
    struct BufferAlloc
    {
        std::mutex* mutex;
        OffsetAllocator::Allocator* allocator;
        OffsetAllocator::Allocation* allocation;
    };

    Core::InlineVector<BufferAlloc, 7> allocated;

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
                  outputModel->modelData.vertexPositionAllocation, rawData.vertices.Size() * sizeof(VertexPosition),
                  "mega vertex position buffer")) {
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

    if (!tryAlloc(resourceManager->indexBufferAllocatorMutex, resourceManager->indexBufferAllocator,
                  outputModel->modelData.indexAllocation, rawData.indices.Size() * sizeof(uint32_t),
                  "mega index buffer")) {
        rollback();
        return false;
    }

    // I'm going to YOLO blas. It needs to be constructed per mesh (as opposed to the typical structure of per-mesh). Lot of work to obtain the necessary information ahead of time.
    // Geometry data is small, there is almost no way there isn't enough space in staging.

    return true;
}

void StaticModelLoadSlot::PrepareUploadData()
{
    // Adjust offsets
    uint32_t vertexOffset = outputModel->modelData.vertexPositionAllocation.offset / sizeof(VertexPosition);
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
            const uint32_t localPi = primitiveIndex.index;
            const Primitive& prim = rawData.primitives[localPi];
            const size_t indexEnd = localPi + 1 < rawData.primitives.Size() ? rawData.primitives[localPi + 1].indexOffset : rawData.indices.Size();
            primitiveIndex.triangleCount = static_cast<uint32_t>((indexEnd - prim.indexOffset) / 3);
            primitiveIndex.boundingBoxMin = prim.boundingBoxMin;
            primitiveIndex.boundingBoxMax = prim.boundingBoxMax;
            primitiveIndex.boundingSphere = prim.boundingSphere;
            primitiveIndex.index += primitiveOffsetCount;
        }
    }

    // Fallback bounds
    if (outputModel->bounds.sphere.radius == 0.f) {
        Core::HeapArray<Vec3> allPositions(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, rawData.vertices.Size());
        for (size_t pi = 0; pi < rawData.primitives.Size(); ++pi) {
            const Primitive& prim = rawData.primitives[pi];
            const Vec3 boundsMin = prim.boundingBoxMin;
            const Vec3 boundsExtents = prim.boundingBoxMax - prim.boundingBoxMin;

            const size_t indexStart = prim.indexOffset;
            const size_t indexEnd = pi + 1 < rawData.primitives.Size() ? rawData.primitives[pi + 1].indexOffset : rawData.indices.Size();

            uint32_t vertMin = UINT32_MAX, vertMax = 0;
            for (size_t ii = indexStart; ii < indexEnd; ++ii) {
                vertMin = std::min(vertMin, rawData.indices[ii]);
                vertMax = std::max(vertMax, rawData.indices[ii]);
            }

            for (uint32_t vi = vertMin; vi <= vertMax; ++vi) {
                allPositions[vi] = AssetLoad::DequantizeVertexPosition(rawData.vertices[vi], boundsMin, boundsExtents);
            }
        }
        outputModel->bounds = ComputeBounds(allPositions);
    }

    // Rebase indices and indexOffset to global for ray-query triangle fetch
    uint32_t indexOffset = outputModel->modelData.indexAllocation.offset / sizeof(uint32_t);
    for (auto& primitive : rawData.primitives) {
        primitive.indexOffset += indexOffset;
    }
    for (uint32_t& index : rawData.indices) {
        index += vertexOffset;
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

    auto uploadBuffer = [&](const void* sourceData, size_t count,
                            size_t srcStride, size_t srcOffset, size_t dstElementSize,
                            VkBuffer targetBuffer, VkDeviceSize targetOffset) {
        size_t uploaded = 0; // in elements

        while (uploaded < count) {
            size_t elemsRemaining = count - uploaded;
            size_t bytesRemaining = elemsRemaining * dstElementSize;
            size_t allocation = stagingAllocator.Allocate(bytesRemaining);

            if (allocation == SIZE_MAX) {
                size_t freeSpace = stagingAllocator.GetRemaining();
                size_t elemsCanFit = freeSpace / dstElementSize;
                if (elemsCanFit == 0) {
                    submitAndWait(true);
                    stagingAllocator.Reset();
                    continue;
                }
                elemsRemaining = std::min(elemsRemaining, elemsCanFit);
                bytesRemaining = elemsRemaining * dstElementSize;
                allocation = stagingAllocator.Allocate(bytesRemaining);
                assert(allocation != SIZE_MAX);
            }

            char* dstPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;

            if (srcStride == dstElementSize && srcOffset == 0) {
                const char* srcPtr = static_cast<const char*>(sourceData) + uploaded * dstElementSize;
                memcpy(dstPtr, srcPtr, bytesRemaining);
            }
            else if (dstElementSize == 8) {
                const char* srcPtr = static_cast<const char*>(sourceData) + uploaded * srcStride + srcOffset;
                for (size_t i = 0; i < elemsRemaining; ++i) {
                    memcpy(dstPtr + i * 8, srcPtr + i * srcStride, 8);
                }
            }
            else if (dstElementSize == 16) {
                const char* srcPtr = static_cast<const char*>(sourceData) + uploaded * srcStride + srcOffset;
                for (size_t i = 0; i < elemsRemaining; ++i) {
                    memcpy(dstPtr + i * 16, srcPtr + i * srcStride, 16);
                }
            }
            else {
                for (size_t i = 0; i < elemsRemaining; ++i) {
                    const char* srcPtr = static_cast<const char*>(sourceData) + (uploaded + i) * srcStride + srcOffset;
                    memcpy(dstPtr + i * dstElementSize, srcPtr, dstElementSize);
                }
            }

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = allocation;
            copyRegion.dstOffset = targetOffset + uploaded * dstElementSize;
            copyRegion.size = bytesRemaining;
            vkCmdCopyBuffer(cmd, stagingBuffer.handle, targetBuffer, 1, &copyRegion);

            uploaded += elemsRemaining;
        }
    };

    const VkDeviceSize positionBufOffset = outputModel->modelData.vertexPositionAllocation.offset;
    const VkDeviceSize attributeBufOffset = (positionBufOffset / sizeof(VertexPosition)) * sizeof(VertexAttribute);

    uploadBuffer(rawData.vertices.Data(), rawData.vertices.Size(),
                 sizeof(Engine::Vertex), 0, sizeof(VertexPosition),
                 resourceManager->megaVertexPositionBuffer.handle, positionBufOffset);

    uploadBuffer(rawData.vertices.Data(), rawData.vertices.Size(),
                 sizeof(Engine::Vertex), offsetof(Engine::Vertex, normalOct), sizeof(VertexAttribute),
                 resourceManager->megaVertexAttributeBuffer.handle, attributeBufOffset);

    uploadBuffer(rawData.meshletVertices.Data(), rawData.meshletVertices.Size(),
                 sizeof(uint32_t), 0, sizeof(uint32_t),
                 resourceManager->megaMeshletVerticesBuffer.handle, outputModel->modelData.meshletVertexAllocation.offset);

    // Pack triangles
    Core::HeapArray<uint32_t> packedTriangles(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, rawData.meshletTriangles.Size() / 3);
    for (size_t i = 0; i < rawData.meshletTriangles.Size(); i += 3) {
        uint32_t packed = rawData.meshletTriangles[i + 0] |
                          (rawData.meshletTriangles[i + 1] << 8) |
                          (rawData.meshletTriangles[i + 2] << 16);
        packedTriangles[i / 3] = packed;
    }

    uploadBuffer(packedTriangles.Data(), packedTriangles.Size(),
                 sizeof(uint32_t), 0, sizeof(uint32_t),
                 resourceManager->megaMeshletTrianglesBuffer.handle, outputModel->modelData.meshletTriangleAllocation.offset);

    uploadBuffer(rawData.meshlets.Data(), rawData.meshlets.Size(),
                 sizeof(Meshlet), 0, sizeof(Meshlet),
                 resourceManager->megaMeshletBuffer.handle, outputModel->modelData.meshletAllocation.offset);

    uploadBuffer(rawData.primitives.Data(), rawData.primitives.Size(),
                 sizeof(Primitive), 0, sizeof(Primitive),
                 resourceManager->primitiveBuffer.handle, outputModel->modelData.primitiveAllocation.offset);

    uploadBuffer(rawData.indices.Data(), rawData.indices.Size(),
                 sizeof(uint32_t), 0, sizeof(uint32_t),
                 resourceManager->megaIndexBuffer.handle, outputModel->modelData.indexAllocation.offset);

    // Queue family transfer barriers
    VkBufferMemoryBarrier2 releaseBarriers[7];
    uint32_t barrierCount = 0;

    auto createBufferBarrier = [&](VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
        VkBufferMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
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

    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaVertexPositionBuffer.handle,
                                                          positionBufOffset, rawData.vertices.Size() * sizeof(VertexPosition));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaVertexAttributeBuffer.handle,
                                                          attributeBufOffset, rawData.vertices.Size() * sizeof(VertexAttribute));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaMeshletVerticesBuffer.handle,
                                                          outputModel->modelData.meshletVertexAllocation.offset, rawData.meshletVertices.Size() * sizeof(uint32_t));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaMeshletTrianglesBuffer.handle,
                                                          outputModel->modelData.meshletTriangleAllocation.offset, rawData.meshletTriangles.Size() / 3 * sizeof(uint32_t));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaMeshletBuffer.handle,
                                                          outputModel->modelData.meshletAllocation.offset, rawData.meshlets.Size() * sizeof(Meshlet));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->primitiveBuffer.handle,
                                                          outputModel->modelData.primitiveAllocation.offset, rawData.primitives.Size() * sizeof(Primitive));
    releaseBarriers[barrierCount++] = createBufferBarrier(resourceManager->megaIndexBuffer.handle,
                                                          outputModel->modelData.indexAllocation.offset, rawData.indices.Size() * sizeof(uint32_t));

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.bufferMemoryBarrierCount = barrierCount;
    depInfo.pBufferMemoryBarriers = releaseBarriers;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    for (uint32_t i = 0; i < barrierCount; ++i) {
        outputModel->bufferAcquireOps.PushBack(Render::VkHelpers::FromVkBarrier(releaseBarriers[i]));
    }
}

bool StaticModelLoadSlot::BuildBLAS(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("BuildBLAS");

    const uint32_t primitiveCount = static_cast<uint32_t>(rawData.primitives.Size());

    const VkDeviceAddress vertexBase = resourceManager->megaVertexPositionBuffer.address;
    const VkDeviceAddress indexBase = resourceManager->megaIndexBuffer.address;
    const uint32_t vertexOffsetCount = outputModel->modelData.vertexPositionAllocation.offset / sizeof(VertexPosition);
    const uint32_t indexOffsetCount = outputModel->modelData.indexAllocation.offset / sizeof(uint32_t); {
        Core::InlineVector<VkBufferMemoryBarrier2, 8> acquireBarriers;
        for (const auto& op : outputModel->bufferAcquireOps) {
            acquireBarriers.PushBack(Render::VkHelpers::ToVkBarrier(op));
        }
        if (!acquireBarriers.IsEmpty()) {
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.bufferMemoryBarrierCount = acquireBarriers.Size();
            depInfo.pBufferMemoryBarriers = acquireBarriers.Data();
            vkCmdPipelineBarrier2(cmd, &depInfo);
        }
    }
    outputModel->bufferAcquireOps.Clear();

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();
    stagingAllocator.Reset();

    const size_t transformBytes = primitiveCount * sizeof(VkTransformMatrixKHR);
    assert(transformBytes <= stagingAllocator.GetCapacity() && "Staging buffer too small for BLAS transform data");
    stagingAllocator.Allocate(transformBytes);

    auto* transforms = reinterpret_cast<VkTransformMatrixKHR*>(static_cast<char*>(stagingBuffer.allocationInfo.pMappedData));
    for (uint32_t i = 0; i < primitiveCount; ++i) {
        const Primitive& prim = rawData.primitives[i];
        const Vec3 extents = prim.boundingBoxMax - prim.boundingBoxMin;
        const Vec3 mn = prim.boundingBoxMin;

        transforms[i].matrix[0][0] = extents.x;
        transforms[i].matrix[0][1] = 0.f;
        transforms[i].matrix[0][2] = 0.f;
        transforms[i].matrix[0][3] = mn.x;
        transforms[i].matrix[1][0] = 0.f;
        transforms[i].matrix[1][1] = extents.y;
        transforms[i].matrix[1][2] = 0.f;
        transforms[i].matrix[1][3] = mn.y;
        transforms[i].matrix[2][0] = 0.f;
        transforms[i].matrix[2][1] = 0.f;
        transforms[i].matrix[2][2] = extents.z;
        transforms[i].matrix[2][3] = mn.z;
    }

    const uint32_t scratchAlignment = Render::VulkanContext::deviceInfo.accelerationStructureProps.minAccelerationStructureScratchOffsetAlignment;
    const int32_t meshCount = static_cast<int>(outputModel->modelData.meshes.Size());
    const uint32_t primitiveOffsetCount = outputModel->modelData.primitiveAllocation.offset / sizeof(Primitive);

    uint32_t buildCount = 0;
    for (int32_t j = 0; j < meshCount; ++j) {
        buildCount += static_cast<uint32_t>(outputModel->modelData.meshes[j].primitiveProperties.Size());
    }

    if (buildCount == 0) {
        submitAndWait(false);
        return true;
    }

    Core::TlsfAllocator* scratchAlloc = &memoryManager->AssetsScratch();
    blasTransients.geoms = Core::HeapArray<VkAccelerationStructureGeometryKHR>(scratchAlloc, Core::AllocTag::AssetModel, buildCount);
    blasTransients.primCounts = Core::HeapArray<uint32_t>(scratchAlloc, Core::AllocTag::AssetModel, buildCount);
    blasTransients.scratchSizes = Core::HeapArray<VkDeviceSize>(scratchAlloc, Core::AllocTag::AssetModel, buildCount);
    blasTransients.buildInfos = Core::HeapArray<VkAccelerationStructureBuildGeometryInfoKHR>(scratchAlloc, Core::AllocTag::AssetModel, buildCount);
    blasTransients.ranges = Core::HeapArray<VkAccelerationStructureBuildRangeInfoKHR>(scratchAlloc, Core::AllocTag::AssetModel, buildCount);
    blasTransients.rangePtrs = Core::HeapArray<const VkAccelerationStructureBuildRangeInfoKHR*>(scratchAlloc, Core::AllocTag::AssetModel, buildCount);

    Core::HeapArray<VkDeviceSize> asSizes(scratchAlloc, Core::AllocTag::AssetModel, buildCount);
    //
    {
        ZoneScopedN("BLAS Size Query");
        uint32_t buildIndex = 0;
        for (int32_t j = 0; j < meshCount; ++j) {
            Engine::MeshInformation& mesh = outputModel->modelData.meshes[j];
            const int32_t meshPrimitiveCount = static_cast<int32_t>(mesh.primitiveProperties.Size());

            for (int32_t i = 0; i < meshPrimitiveCount; ++i) {
                const Engine::PrimitiveProperty& props = mesh.primitiveProperties[i];
                const uint32_t realPrimitiveIndex = props.index - primitiveOffsetCount;
                const Primitive& prim = rawData.primitives[realPrimitiveIndex];

                const uint32_t indexStart = prim.indexOffset;
                const uint32_t indexEnd = (realPrimitiveIndex + 1 < primitiveCount) ? rawData.primitives[realPrimitiveIndex + 1].indexOffset : indexOffsetCount + static_cast<uint32_t>(rawData.indices.Size());

                VkAccelerationStructureGeometryKHR& geom = blasTransients.geoms[buildIndex];
                geom = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
                geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
                auto& tri = geom.geometry.triangles;
                tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                tri.vertexFormat = VK_FORMAT_R16G16B16A16_UNORM;
                tri.vertexData.deviceAddress = vertexBase;
                tri.vertexStride = sizeof(VertexPosition);
                tri.maxVertex = vertexOffsetCount + static_cast<uint32_t>(rawData.vertices.Size()) - 1;
                tri.indexType = VK_INDEX_TYPE_UINT32;
                tri.indexData.deviceAddress = indexBase + indexStart * sizeof(uint32_t);
                tri.transformData.deviceAddress = stagingBuffer.address + realPrimitiveIndex * sizeof(VkTransformMatrixKHR);

                blasTransients.primCounts[buildIndex] = (indexEnd - indexStart) / 3;

                VkAccelerationStructureBuildGeometryInfoKHR& buildInfo = blasTransients.buildInfos[buildIndex];
                buildInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
                buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
                buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                buildInfo.geometryCount = 1;
                buildInfo.pGeometries = &geom;

                VkAccelerationStructureBuildSizesInfoKHR sizeInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
                vkGetAccelerationStructureBuildSizesKHR(context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &blasTransients.primCounts[buildIndex], &sizeInfo);

                const VkDeviceSize scratchSize = (sizeInfo.buildScratchSize + scratchAlignment - 1ull) & ~(scratchAlignment - 1ull);
                blasTransients.scratchSizes[buildIndex] = scratchSize;
                asSizes[buildIndex] = sizeInfo.accelerationStructureSize;

                blasTransients.ranges[buildIndex] = {.primitiveCount = blasTransients.primCounts[buildIndex]};
                blasTransients.rangePtrs[buildIndex] = &blasTransients.ranges[buildIndex];
                ++buildIndex;
            }
        }
    }

    if (blasScratch.handle == VK_NULL_HANDLE) {
        VkBufferCreateInfo scratchBufInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        scratchBufInfo.size = BLAS_SCRATCH_SLOT_SIZE;
        scratchBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VmaAllocationCreateInfo scratchAllocInfo{};
        scratchAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        blasScratch = Render::AllocatedBuffer::CreateAllocatedBufferAligned(context, scratchBufInfo, scratchAllocInfo, scratchAlignment);
    }

    const VkDeviceSize poolSize = blasScratch.size;

    uint32_t batchStart = 0;
    VkDeviceSize scratchOffset = 0;
    auto flushBatch = [&](uint32_t batchEnd, bool bReset) {
        if (batchEnd > batchStart) {
            vkCmdBuildAccelerationStructuresKHR(cmd, batchEnd - batchStart, &blasTransients.buildInfos[batchStart], &blasTransients.rangePtrs[batchStart]);
        }
        submitAndWait(bReset);
        batchStart = batchEnd;
        scratchOffset = 0;
    };

    uint32_t buildIndex = 0;
    for (int32_t j = 0; j < meshCount; ++j) {
        Engine::MeshInformation& mesh = outputModel->modelData.meshes[j];
        const int32_t meshPrimitiveCount = static_cast<int32_t>(mesh.primitiveProperties.Size());

        for (int32_t i = 0; i < meshPrimitiveCount; ++i) {
            Engine::PrimitiveProperty& props = mesh.primitiveProperties[i];

            const VkDeviceSize scratchSize = blasTransients.scratchSizes[buildIndex];
            const bool bOversized = scratchSize > poolSize;
            if (bOversized || scratchOffset + scratchSize > poolSize) {
                flushBatch(buildIndex, true);
            }

            const VkDeviceSize alignedASSize = (asSizes[buildIndex] + 255ull) & ~255ull; {
                std::lock_guard lock(resourceManager->blasBufferAllocatorMutex);
                props.blasAllocation = resourceManager->blasBufferAllocator.allocate(alignedASSize);
            }
            if (props.blasAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                SPDLOG_ERROR("[StaticModelLoadSlot] No space in mega BLAS buffer for primitive {} of mesh {} of {}", i, j, outputModel->name.c_str());
                return false;
            }

            VkAccelerationStructureCreateInfoKHR createInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            createInfo.buffer = resourceManager->megaBLASBuffer.handle;
            createInfo.offset = props.blasAllocation.offset;
            createInfo.size = asSizes[buildIndex];
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            VkAccelerationStructureKHR blas{};
            VK_CHECK(vkCreateAccelerationStructureKHR(context->device, &createInfo, context->HostAllocCallbacks(), &blas));
            props.blasHandle = reinterpret_cast<uint64_t>(blas);

            VkAccelerationStructureDeviceAddressInfoKHR addrInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
            addrInfo.accelerationStructure = blas;
            props.blasDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(context->device, &addrInfo);

            blasTransients.buildInfos[buildIndex].dstAccelerationStructure = blas;

            if (bOversized) {
                SPDLOG_WARN("[StaticModelLoadSlot] {} mesh {} primitive {} needs {} bytes of BLAS scratch ({} triangles), slot buffer is {}. Building it alone on a temporary buffer; split the primitive.",
                            outputModel->name.c_str(), j, i, scratchSize, blasTransients.primCounts[buildIndex], poolSize);

                VkBufferCreateInfo oversizedBufInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                oversizedBufInfo.size = scratchSize;
                oversizedBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                VmaAllocationCreateInfo oversizedAllocInfo{};
                oversizedAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                Render::AllocatedBuffer oversizedScratch = Render::AllocatedBuffer::CreateAllocatedBufferAligned(context, oversizedBufInfo, oversizedAllocInfo, scratchAlignment);

                blasTransients.buildInfos[buildIndex].scratchData.deviceAddress = oversizedScratch.address;
                flushBatch(buildIndex + 1, true);
            }
            else {
                blasTransients.buildInfos[buildIndex].scratchData.deviceAddress = blasScratch.address + scratchOffset;
                scratchOffset += scratchSize;
            }
            ++buildIndex;
        }
    }

    flushBatch(buildCount, false);
    return true;
}
} // AssetLoad
