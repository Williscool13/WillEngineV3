//
// Created by William on 2025-12-23.
//

#include "static_model_load_slot.h"

#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "engine/resources/model/model_serialization.h"
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
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    std::function<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
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


    if (task && !task->GetIsComplete()) {
        scheduler->WaitforTask(task.get());
    }
    task = std::make_unique<LoadModelTask>();
    task->loadSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void StaticModelLoadSlot::Clear()
{
    modelSlotHandle = {};
    uploadStagingSlotHandle = {};
    outputModel = nullptr;
    uploadStaging = nullptr;

    rawData.Reset();
    packedTriangles.clear();
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
    ZoneScopedN("LoadModelTask");
    //
    {
        ZoneScopedN("FileExistsCheck");
        if (!std::filesystem::exists(outputModel->source)) {
            SPDLOG_ERROR("Failed to find path to static model - {}", outputModel->modelId.ToString());
            return false;
        }
    }

    Engine::ModelReader reader = Engine::ModelReader(outputModel->source);

    if (!reader.GetSuccessfullyLoaded()) {
        SPDLOG_ERROR("Failed to load static model - {}", outputModel->modelId.ToString());
        return false;
    }

    std::vector<uint8_t> modelBinData; {
        ZoneScopedN("ReadModelBin");
        modelBinData = reader.ReadFile("model.bin");
    }

    size_t offset = 0;
    const auto* header = reinterpret_cast<Engine::ModelBinaryHeader*>(modelBinData.data());
    offset += sizeof(Engine::ModelBinaryHeader);

    auto readArray = [&]<typename T>(std::vector<T>& vec, uint32_t count) {
        vec.resize(count);
        if (count > 0) {
            std::memcpy(vec.data(), modelBinData.data() + offset, count * sizeof(T));
            offset += count * sizeof(T);
        }
    };

    const uint8_t* dataPtr = modelBinData.data() + offset;

    {
        ZoneScopedN("ParseGeometryData");
        readArray(rawData.vertices, header->vertexCount);
        readArray(rawData.meshletVertices, header->meshletVertexCount);
        readArray(rawData.meshletTriangles, header->meshletTriangleCount);
        readArray(rawData.meshlets, header->meshletCount);
        readArray(rawData.primitives, header->primitiveCount);
    }

    {
        ZoneScopedN("ParseMaterials");
        const uint8_t* matPtr = modelBinData.data() + offset;
        rawData.materials.resize(header->materialCount);
        for (uint32_t i = 0; i < header->materialCount; ++i) {
            Engine::ReadMaterial(matPtr, rawData.materials[i]);
        }
        offset = matPtr - modelBinData.data();
    }

    dataPtr = modelBinData.data() + offset; {
        ZoneScopedN("ParseMeshes");
        rawData.allMeshes.resize(header->meshCount);
        for (uint32_t i = 0; i < header->meshCount; i++) {
            Engine::ReadMeshInformation(dataPtr, rawData.allMeshes[i]);
        }
    } {
        ZoneScopedN("ParseAnimations");
        rawData.animations.resize(header->animationCount);
        for (uint32_t i = 0; i < header->animationCount; i++) {
            Engine::ReadAnimation(dataPtr, rawData.animations[i]);
        }
    }

    offset = dataPtr - modelBinData.data(); {
        ZoneScopedN("ParseSkeletalData");
        readArray(rawData.inverseBindMatrices, header->inverseBindMatrixCount);
    }

    {
        // Technically we can get nodes from metadata, but I don't think it matters.
        ZoneScopedN("ParseNodes");
        reader.ReadNodes(rawData.nodes);
    }

    return true;
}

bool StaticModelLoadSlot::AllocateGPUResources() const
{
    // Thread-safe allocation (mutexes are expensive but this is rather infrequent)
    size_t sizeVertices = rawData.vertices.size() * sizeof(Vertex);
    {
        std::lock_guard lock(resourceManager->vertexBufferAllocatorMutex);
        outputModel->modelData.vertexAllocation = resourceManager->vertexBufferAllocator.allocate(sizeVertices);
        if (outputModel->modelData.vertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            SPDLOG_ERROR("[StaticModelLoadSlot] Not enough space in mega vertex buffer");
            return false;
        }
    }

    size_t sizeMeshletVertices = rawData.meshletVertices.size() * sizeof(uint32_t);
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

    size_t sizeMeshletTriangles = rawData.meshletTriangles.size() / 3 * sizeof(uint32_t);
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

    size_t sizeMeshlets = rawData.meshlets.size() * sizeof(Meshlet);
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

    size_t sizePrimitives = rawData.primitives.size() * sizeof(MeshletPrimitive);
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

    uint32_t primitiveOffsetCount = outputModel->modelData.primitiveAllocation.offset / sizeof(MeshletPrimitive);
    for (auto& mesh : rawData.allMeshes) {
        for (auto& primitiveIndex : mesh.primitiveProperties) {
            primitiveIndex.index += primitiveOffsetCount;
        }
    }

    // Move data to outputModel
    outputModel->modelData.meshes = std::move(rawData.allMeshes);
    outputModel->modelData.nodes = std::move(rawData.nodes);
    outputModel->modelData.inverseBindMatrices = std::move(rawData.inverseBindMatrices);
    outputModel->modelData.animations = std::move(rawData.animations);
    outputModel->modelData.materials = std::move(rawData.materials);

    // Pack triangles
    packedTriangles.reserve(rawData.meshletTriangles.size() / 3);
    for (size_t i = 0; i < rawData.meshletTriangles.size(); i += 3) {
        uint32_t packed = rawData.meshletTriangles[i + 0] |
                          (rawData.meshletTriangles[i + 1] << 8) |
                          (rawData.meshletTriangles[i + 2] << 16);
        packedTriangles.push_back(packed);
    }
}

void StaticModelLoadSlot::UploadGeometry(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait)
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

    uploadBuffer(rawData.vertices.data(), rawData.vertices.size(), sizeof(Vertex),
                resourceManager->megaVertexBuffer.handle, outputModel->modelData.vertexAllocation.offset);

    uploadBuffer(rawData.meshletVertices.data(), rawData.meshletVertices.size(), sizeof(uint32_t),
                resourceManager->megaMeshletVerticesBuffer.handle, outputModel->modelData.meshletVertexAllocation.offset);

    uploadBuffer(packedTriangles.data(), packedTriangles.size(), sizeof(uint32_t),
                resourceManager->megaMeshletTrianglesBuffer.handle, outputModel->modelData.meshletTriangleAllocation.offset);

    uploadBuffer(rawData.meshlets.data(), rawData.meshlets.size(), sizeof(Meshlet),
                resourceManager->megaMeshletBuffer.handle, outputModel->modelData.meshletAllocation.offset);

    uploadBuffer(rawData.primitives.data(), rawData.primitives.size(), sizeof(MeshletPrimitive),
                resourceManager->primitiveBuffer.handle, outputModel->modelData.primitiveAllocation.offset);

    // Queue family transfer barriers
    std::vector<VkBufferMemoryBarrier2> releaseBarriers;

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

    releaseBarriers.push_back(createBufferBarrier(resourceManager->megaVertexBuffer.handle,
        outputModel->modelData.vertexAllocation.offset, rawData.vertices.size() * sizeof(Vertex)));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->megaMeshletVerticesBuffer.handle,
        outputModel->modelData.meshletVertexAllocation.offset, rawData.meshletVertices.size() * sizeof(uint32_t)));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->megaMeshletTrianglesBuffer.handle,
        outputModel->modelData.meshletTriangleAllocation.offset, rawData.meshletTriangles.size() / 3 * sizeof(uint32_t)));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->megaMeshletBuffer.handle,
        outputModel->modelData.meshletAllocation.offset, rawData.meshlets.size() * sizeof(Meshlet)));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->primitiveBuffer.handle,
        outputModel->modelData.primitiveAllocation.offset, rawData.primitives.size() * sizeof(MeshletPrimitive)));

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.bufferMemoryBarrierCount = releaseBarriers.size();
    depInfo.pBufferMemoryBarriers = releaseBarriers.data();
    vkCmdPipelineBarrier2(cmd, &depInfo);

    for (auto& barrier : releaseBarriers) {
        outputModel->bufferAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));
    }
}
} // AssetLoad
