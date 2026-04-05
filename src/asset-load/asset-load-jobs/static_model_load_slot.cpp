//
// Created by William on 2025-12-23.
//

#include "static_model_load_slot.h"

#include <fstream>
#include <semaphore>

#include "asset-load/asset_load_config.h"
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
    Core::TlsfAllocator* _allocator,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    packedTriangles = Core::Vector<uint32_t>(_allocator, Core::AllocTag::AssetModel);
    rawData.Init(_allocator);
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

    rawData.Reset();
    packedTriangles.Clear();
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
    std::vector<uint8_t> body;
    std::vector<uint8_t> nodeData; {
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

        std::vector<uint8_t> compressedBody(header.compressedBodySize);
        file.seekg(static_cast<std::streamoff>(header.dataOffset));
        file.read(reinterpret_cast<char*>(compressedBody.data()), static_cast<std::streamsize>(header.compressedBodySize));

        body = Engine::DecompressLZ4(compressedBody.data(), compressedBody.size(), header.uncompressedBodySize);

        file.seekg(0, std::ios::end);
        const size_t fileSize = static_cast<size_t>(file.tellg());
        const size_t nodeDataStart = header.dataOffset + header.compressedBodySize;
        nodeData.resize(fileSize - nodeDataStart);
        file.seekg(static_cast<std::streamoff>(nodeDataStart));
        file.read(reinterpret_cast<char*>(nodeData.data()), static_cast<std::streamsize>(nodeData.size()));
    }

    auto readArray = [&]<typename T>(Core::Vector<T>& vec, uint32_t offset, uint32_t count) {
        vec.Resize(count);
        if (count > 0) {
            std::memcpy(vec.Data(), body.data() + offset, count * sizeof(T));
        }
    }; {
        ZoneScopedN("ParseGeometryData");
        readArray(rawData.vertices, header.vertexOffset, header.vertexCount);
        readArray(rawData.indices, header.indexOffset, header.indexCount);
        readArray(rawData.meshletVertices, header.meshletVertexOffset, header.meshletVertexCount);
        readArray(rawData.meshletTriangles, header.meshletTriangleOffset, header.meshletTriangleCount);
        readArray(rawData.meshlets, header.meshletOffset, header.meshletCount);
        readArray(rawData.primitives, header.primitiveOffset, header.primitiveCount);
    } {
        ZoneScopedN("ParseMaterials");
        const uint8_t* ptr = body.data() + header.materialOffset;
        rawData.materials.Resize(header.materialCount);
        for (uint32_t i = 0; i < header.materialCount; ++i) {
            Engine::ReadMaterial(ptr, rawData.materials[i]);
        }
    } {
        ZoneScopedN("ParseMeshes");
        const uint8_t* ptr = body.data() + header.meshOffset;
        rawData.allMeshes.Resize(header.meshCount);
        for (uint32_t i = 0; i < header.meshCount; ++i) {
            Engine::ReadMeshInformation(ptr, rawData.allMeshes[i]);
        }
    } {
        ZoneScopedN("ParseNodes");
        const uint8_t* ptr = nodeData.data();
        rawData.nodes.Resize(header.nodeCount);
        for (uint32_t i = 0; i < header.nodeCount; ++i) {
            Engine::ReadNode(ptr, rawData.nodes[i]);
        }

        const size_t bytesConsumed = ptr - nodeData.data();
        const size_t remaining = nodeData.size() - bytesConsumed;
        if (remaining >= sizeof(Engine::ModelBounds)) {
            memcpy(&outputModel->bounds, ptr, sizeof(Engine::ModelBounds));
        }
    }

    return true;
}

bool StaticModelLoadSlot::AllocateGPUResources() const
{
    // Thread-safe allocation (mutexes are expensive but this is rather infrequent)
    size_t sizeVertices = rawData.vertices.Size() * sizeof(Vertex); {
        std::lock_guard lock(resourceManager->vertexBufferAllocatorMutex);
        outputModel->modelData.vertexAllocation = resourceManager->vertexBufferAllocator.allocate(sizeVertices);
        if (outputModel->modelData.vertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            SPDLOG_ERROR("[StaticModelLoadSlot] Not enough space in mega vertex buffer");
            return false;
        }
    }

    size_t sizeMeshletVertices = rawData.meshletVertices.Size() * sizeof(uint32_t);
    //
    {
        std::lock_guard lock(resourceManager->meshletVertexBufferAllocatorMutex);
        outputModel->modelData.meshletVertexAllocation = resourceManager->meshletVertexBufferAllocator.allocate(sizeMeshletVertices);
        if (outputModel->modelData.meshletVertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            std::lock_guard cleanupLock(resourceManager->vertexBufferAllocatorMutex);
            resourceManager->vertexBufferAllocator.free(outputModel->modelData.vertexAllocation);
            SPDLOG_ERROR("[StaticModelLoadSlot] Not enough space in mega meshlet vertex buffer");
            return false;
        }
    }

    size_t sizeMeshletTriangles = rawData.meshletTriangles.Size() / 3 * sizeof(uint32_t);
    //
    {
        std::lock_guard lock(resourceManager->meshletTriangleBufferAllocatorMutex);
        outputModel->modelData.meshletTriangleAllocation = resourceManager->meshletTriangleBufferAllocator.allocate(sizeMeshletTriangles);
        if (outputModel->modelData.meshletTriangleAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            // Cleanup previous allocations
            {
                std::lock_guard cleanupLock(resourceManager->vertexBufferAllocatorMutex);
                resourceManager->vertexBufferAllocator.free(outputModel->modelData.vertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletVertexBufferAllocatorMutex);
                resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
            }
            SPDLOG_ERROR("[StaticModelLoadSlot] Not enough space in mega meshlet triangle buffer");
            return false;
        }
    }

    size_t sizeMeshlets = rawData.meshlets.Size() * sizeof(Meshlet);
    //
    {
        std::lock_guard lock(resourceManager->meshletBufferAllocatorMutex);
        outputModel->modelData.meshletAllocation = resourceManager->meshletBufferAllocator.allocate(sizeMeshlets);
        if (outputModel->modelData.meshletAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            // Cleanup all previous allocations
            {
                std::lock_guard cleanupLock(resourceManager->vertexBufferAllocatorMutex);
                resourceManager->vertexBufferAllocator.free(outputModel->modelData.vertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletVertexBufferAllocatorMutex);
                resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletTriangleBufferAllocatorMutex);
                resourceManager->meshletTriangleBufferAllocator.free(outputModel->modelData.meshletTriangleAllocation);
            }
            SPDLOG_ERROR("[StaticModelLoadSlot] Not enough space in mega meshlet buffer");
            return false;
        }
    }

    size_t sizePrimitives = rawData.primitives.Size() * sizeof(Primitive);
    //
    {
        std::lock_guard lock(resourceManager->primitiveBufferAllocatorMutex);
        outputModel->modelData.primitiveAllocation = resourceManager->primitiveBufferAllocator.allocate(sizePrimitives);
        if (outputModel->modelData.primitiveAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            // Cleanup all previous allocations
            {
                std::lock_guard cleanupLock(resourceManager->vertexBufferAllocatorMutex);
                resourceManager->vertexBufferAllocator.free(outputModel->modelData.vertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletVertexBufferAllocatorMutex);
                resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletTriangleBufferAllocatorMutex);
                resourceManager->meshletTriangleBufferAllocator.free(outputModel->modelData.meshletTriangleAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletBufferAllocatorMutex);
                resourceManager->meshletBufferAllocator.free(outputModel->modelData.meshletAllocation);
            }
            SPDLOG_ERROR("[StaticModelLoadSlot] Not enough space in mega primitive buffer");
            return false;
        }
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
    } {
        std::vector<glm::vec3> positions;
        positions.reserve(rawData.vertices.Size());
        for (const auto& v : rawData.vertices) {
            positions.push_back(v.position);
        }

        Engine::StaticModel::PhysicsCache cache;
        cache.positions = positions;
        cache.indices.assign(rawData.indices.begin(), rawData.indices.end());

        if (outputModel->bounds.sphere.radius == 0.f) {
            outputModel->bounds = Engine::StaticModel::ComputeBounds(positions, &cache.indices);
        }

        constexpr size_t kSimplifyThreshold = 1500;
        constexpr size_t kSimplifyFloor = 1500;
        constexpr float kSimplifyRatio = 0.15f;
        constexpr float kSimplifyError = 0.01f;
        if (cache.indices.size() > kSimplifyThreshold) {
            const size_t target = std::max(kSimplifyFloor, static_cast<size_t>(cache.indices.size() * kSimplifyRatio));
            std::vector<uint32_t> simplified(cache.indices.size());
            const size_t result = meshopt_simplify(
                simplified.data(),
                cache.indices.data(), cache.indices.size(),
                &cache.positions[0].x, cache.positions.size(), sizeof(glm::vec3),
                target, kSimplifyError
            );
            simplified.resize(result);
            cache.indices = std::move(simplified);
        }

        outputModel->physicsCache = std::move(cache);
    }

    // Move geometry data to outputModel (StaticModelData uses std::vector)
    {
        auto& dst = outputModel->modelData.meshes;
        dst.clear();
        dst.reserve(rawData.allMeshes.Size());
        for (auto& m : rawData.allMeshes) { dst.push_back(std::move(m)); }
    }
    {
        auto& dst = outputModel->modelData.nodes;
        dst.clear();
        dst.reserve(rawData.nodes.Size());
        for (auto& n : rawData.nodes) { dst.push_back(std::move(n)); }
    }
    {
        auto& dst = outputModel->modelData.materials;
        dst.clear();
        dst.reserve(rawData.materials.Size());
        for (auto& m : rawData.materials) { dst.push_back(std::move(m)); }
    }

    // Pack triangles
    packedTriangles.Reserve(rawData.meshletTriangles.Size() / 3);
    for (size_t i = 0; i < rawData.meshletTriangles.Size(); i += 3) {
        uint32_t packed = rawData.meshletTriangles[i + 0] |
                          (rawData.meshletTriangles[i + 1] << 8) |
                          (rawData.meshletTriangles[i + 2] << 16);
        packedTriangles.PushBack(packed);
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
