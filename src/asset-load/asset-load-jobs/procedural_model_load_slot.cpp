//
// Created by William on 2026-03-14.
//

#include "procedural_model_load_slot.h"

#include "asset-load/asset_load_config.h"
#include "core/overloaded.h"
#include "engine/resources/model/static_model.h"
#include "render/resource_manager.h"
#include "render/shaders/constants_interop.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

#include "par/par_shapes.h"
#include "meshoptimizer/src/meshoptimizer.h"

namespace AssetLoad
{
ProceduralModelLoadSlot::ProceduralModelLoadSlot() = default;

ProceduralModelLoadSlot::~ProceduralModelLoadSlot() = default;

void ProceduralModelLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    std::function<void(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);
}

void ProceduralModelLoadSlot::Launch(
    ProceduralModelSlotHandle _slotHandle,
    UploadStagingSlotHandle _uploadStagingSlotHandle,
    UploadStaging* _uploadStaging,
    Engine::StaticModel* _outputModel)
{
    slotHandle = _slotHandle;
    uploadStagingSlotHandle = _uploadStagingSlotHandle;
    uploadStaging = _uploadStaging;
    outputModel = _outputModel;

    if (task && !task->GetIsComplete()) {
        scheduler->WaitforTask(task.get());
    }
    task = std::make_unique<GenerateModelTask>();
    task->loadSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void ProceduralModelLoadSlot::Clear()
{
    slotHandle = {};
    uploadStagingSlotHandle = {};
    outputModel = nullptr;
    uploadStaging = nullptr;

    rawData.Reset();
    packedTriangles.clear();
}

void ProceduralModelLoadSlot::GenerateModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->GenerateGeometry()) {
        loadSlot->_notifyCallback(false, loadSlot->slotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->slotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    loadSlot->PrepareUploadData();

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(loadSlot->context->transferQueueFamily);
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(loadSlot->context->device, &poolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, commandPool);
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(loadSlot->context->device, &cmdInfo, &cmd));

    VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
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

            VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
        }
    };

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    loadSlot->UploadGeometry(cmd, submitAndWait);

    VK_CHECK(vkEndCommandBuffer(cmd));
    std::binary_semaphore done(0);
    loadSlot->_requestDispatchCallback(cmd, fence, &done);
    done.acquire();

    vkDestroyFence(loadSlot->context->device, fence, nullptr);
    vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);

    loadSlot->_notifyCallback(true, loadSlot->slotHandle, loadSlot->uploadStagingSlotHandle);
}

bool ProceduralModelLoadSlot::GenerateGeometry()
{
    ZoneScopedN("GenerateGeometry");

    Engine::ProceduralParams& params = outputModel->proceduralParams;

    bool bSuccess = false;
    std::visit(overloaded{
                   [](std::monostate) {},
                   [&](const Engine::StaircaseParams& p) {
                       bSuccess = GenerateStaircase(p);
                   },
                   [&](const Engine::BoxParams& p) {
                       // TODO
                       bSuccess = true;
                   },
               }, params);
    return bSuccess;
}

bool ProceduralModelLoadSlot::GenerateStaircase(const Engine::StaircaseParams& p)
{
    ZoneScopedN("GenerateStaircase");

    if (p.stepCount <= 0) return false;

    // Build each step as a scaled cube, merge into one mesh.
    // Step i occupies: X[-width/2, width/2], Y[0, (i+1)*stepHeight], Z[i*stepDepth, (i+1)*stepDepth]
    par_shapes_mesh* merged = nullptr;
    for (int32_t i = 0; i < p.stepCount; ++i) {
        par_shapes_mesh* step = par_shapes_create_cube();
        par_shapes_scale(step, p.width, static_cast<float>(i + 1) * p.stepHeight, p.stepDepth);
        par_shapes_translate(step, -p.width * 0.5f, 0.0f, static_cast<float>(i) * p.stepDepth);
        if (!merged) {
            merged = step;
        }
        else {
            par_shapes_merge_and_free(merged, step);
        }
    }

    if (!merged) { return false; }

    par_shapes_compute_normals(merged);

    // Convert to engine vertex format
    std::vector<Vertex> vertices(merged->npoints);
    for (int32_t i = 0; i < merged->npoints; ++i) {
        vertices[i].position = {merged->points[i * 3 + 0], merged->points[i * 3 + 1], merged->points[i * 3 + 2]};
        vertices[i].normal = merged->normals ? glm::vec3(merged->normals[i * 3 + 0], merged->normals[i * 3 + 1], merged->normals[i * 3 + 2]) : glm::vec3(0, 1, 0);
        vertices[i].texcoordU = merged->tcoords ? merged->tcoords[i * 2 + 0] : 0.0f;
        vertices[i].texcoordV = merged->tcoords ? merged->tcoords[i * 2 + 1] : 0.0f;
        vertices[i].tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    std::vector<uint32_t> indices(merged->ntriangles * 3);
    for (int32_t i = 0; i < merged->ntriangles * 3; ++i) {
        indices[i] = static_cast<uint32_t>(merged->triangles[i]);
    }

    par_shapes_free_mesh(merged);

    // Meshoptimizer pipeline
    {
        std::vector<uint32_t> remap(vertices.size());
        size_t uniqueVertices = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));

        std::vector<uint32_t> remappedIndices(indices.size());
        meshopt_remapIndexBuffer(remappedIndices.data(), indices.data(), indices.size(), remap.data());

        std::vector<Vertex> remappedVertices(uniqueVertices);
        meshopt_remapVertexBuffer(remappedVertices.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap.data());

        meshopt_optimizeVertexCache(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(), uniqueVertices);
        meshopt_optimizeOverdraw(remappedIndices.data(), remappedIndices.data(), remappedIndices.size(), &remappedVertices[0].position.x, uniqueVertices, sizeof(Vertex), 1.05f);
        meshopt_optimizeVertexFetch(remappedVertices.data(), remappedIndices.data(), remappedIndices.size(), remappedVertices.data(), uniqueVertices, sizeof(Vertex));

        vertices = std::move(remappedVertices);
        indices = std::move(remappedIndices);
    }

    // Build meshlets (LOD0 only, replicated across all LOD slots)
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t> meshletVertices;
    std::vector<uint8_t> meshletTriangles; {
        size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES);
        meshlets.resize(maxMeshlets);
        meshletVertices.resize(maxMeshlets * MESHLET_MAX_VERTICES);
        meshletTriangles.resize(maxMeshlets * MESHLET_MAX_TRIANGLES * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
            indices.data(), indices.size(),
            &vertices[0].position.x, vertices.size(), sizeof(Vertex),
            MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES, 0.0f);

        meshlets.resize(meshletCount);
        const meshopt_Meshlet& last = meshlets.back();
        meshletVertices.resize(last.vertex_offset + last.vertex_count);
        meshletTriangles.resize(last.triangle_offset + last.triangle_count * 3);

        for (auto& m : meshlets) {
            meshopt_optimizeMeshlet(&meshletVertices[m.vertex_offset], &meshletTriangles[m.triangle_offset], m.triangle_count, m.vertex_count);
        }
    }

    // Bounding sphere
    glm::vec3 center{0};
    for (auto& v : vertices) center += v.position;
    center /= static_cast<float>(vertices.size());
    float radius = 0.0f;
    for (auto& v : vertices) radius = std::max(radius, glm::dot(v.position - center, v.position - center));
    radius = std::nextafter(sqrtf(radius), std::numeric_limits<float>::max());

    // Fill rawData
    uint32_t vertexOffset = static_cast<uint32_t>(rawData.vertices.size());
    uint32_t meshletVertexOffset = static_cast<uint32_t>(rawData.meshletVertices.size());
    uint32_t meshletTriangleOffset = static_cast<uint32_t>(rawData.meshletTriangles.size());
    uint32_t meshletBaseOffset = static_cast<uint32_t>(rawData.meshlets.size());

    rawData.vertices.insert(rawData.vertices.end(), vertices.begin(), vertices.end());

    uint32_t indexOffset = static_cast<uint32_t>(rawData.indices.size());
    for (uint32_t idx : indices) rawData.indices.push_back(idx + vertexOffset);

    rawData.meshletVertices.insert(rawData.meshletVertices.end(), meshletVertices.begin(), meshletVertices.end());
    rawData.meshletTriangles.insert(rawData.meshletTriangles.end(), meshletTriangles.begin(), meshletTriangles.end());

    for (auto& m : meshlets) {
        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &meshletVertices[m.vertex_offset], &meshletTriangles[m.triangle_offset], m.triangle_count,
            reinterpret_cast<const float*>(vertices.data()), vertices.size(), sizeof(Vertex));

        rawData.meshlets.push_back({
            .meshletBoundingSphere = {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius},
            .coneApex = {bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]},
            .coneCutoff = bounds.cone_cutoff,
            .coneAxis = {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]},
            .vertexOffset = vertexOffset,
            .meshletVertexOffset = meshletVertexOffset + m.vertex_offset,
            .meshletTriangleOffset = meshletTriangleOffset + m.triangle_offset,
            .meshletVertexCount = m.vertex_count,
            .meshletTriangleCount = m.triangle_count,
        });
    }

    uint32_t meshletCount = static_cast<uint32_t>(meshlets.size());
    Primitive primitiveData{};
    primitiveData.meshletOffset = glm::ivec4(meshletBaseOffset);
    primitiveData.meshletCount = glm::ivec4(meshletCount);
    primitiveData.boundingSphere = {center, radius};
    primitiveData.bHasTransparent = 0;
    primitiveData.indexOffset = indexOffset;

    Engine::MeshInformation meshInfo;
    meshInfo.name = outputModel->name;
    meshInfo.primitiveProperties.push_back({static_cast<uint32_t>(rawData.primitives.size()), -1});

    rawData.primitives.push_back(primitiveData);
    rawData.allMeshes.push_back(std::move(meshInfo));

    return true;
}

bool ProceduralModelLoadSlot::AllocateGPUResources() const
{
    size_t sizeVertices = rawData.vertices.size() * sizeof(Vertex); {
        std::lock_guard lock(resourceManager->vertexBufferAllocatorMutex);
        outputModel->modelData.vertexAllocation = resourceManager->vertexBufferAllocator.allocate(sizeVertices);
        if (outputModel->modelData.vertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            SPDLOG_ERROR("[ProceduralModelLoadSlot] Not enough space in mega vertex buffer");
            return false;
        }
    }

    size_t sizeMeshletVertices = rawData.meshletVertices.size() * sizeof(uint32_t); {
        std::lock_guard lock(resourceManager->meshletVertexBufferAllocatorMutex);
        outputModel->modelData.meshletVertexAllocation = resourceManager->meshletVertexBufferAllocator.allocate(sizeMeshletVertices);
        if (outputModel->modelData.meshletVertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            std::lock_guard cleanupLock(resourceManager->vertexBufferAllocatorMutex);
            resourceManager->vertexBufferAllocator.free(outputModel->modelData.vertexAllocation);
            SPDLOG_ERROR("[ProceduralModelLoadSlot] Not enough space in mega meshlet vertex buffer");
            return false;
        }
    }

    size_t sizeMeshletTriangles = rawData.meshletTriangles.size() / 3 * sizeof(uint32_t); {
        std::lock_guard lock(resourceManager->meshletTriangleBufferAllocatorMutex);
        outputModel->modelData.meshletTriangleAllocation = resourceManager->meshletTriangleBufferAllocator.allocate(sizeMeshletTriangles);
        if (outputModel->modelData.meshletTriangleAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            {
                std::lock_guard cleanupLock(resourceManager->vertexBufferAllocatorMutex);
                resourceManager->vertexBufferAllocator.free(outputModel->modelData.vertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletVertexBufferAllocatorMutex);
                resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
            }
            SPDLOG_ERROR("[ProceduralModelLoadSlot] Not enough space in mega meshlet triangle buffer");
            return false;
        }
    }

    size_t sizeMeshlets = rawData.meshlets.size() * sizeof(Meshlet); {
        std::lock_guard lock(resourceManager->meshletBufferAllocatorMutex);
        outputModel->modelData.meshletAllocation = resourceManager->meshletBufferAllocator.allocate(sizeMeshlets);
        if (outputModel->modelData.meshletAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
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
            SPDLOG_ERROR("[ProceduralModelLoadSlot] Not enough space in mega meshlet buffer");
            return false;
        }
    }

    size_t sizePrimitives = rawData.primitives.size() * sizeof(Primitive); {
        std::lock_guard lock(resourceManager->primitiveBufferAllocatorMutex);
        outputModel->modelData.primitiveAllocation = resourceManager->primitiveBufferAllocator.allocate(sizePrimitives);
        if (outputModel->modelData.primitiveAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
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
            SPDLOG_ERROR("[ProceduralModelLoadSlot] Not enough space in mega primitive buffer");
            return false;
        }
    }

    return true;
}

void ProceduralModelLoadSlot::PrepareUploadData()
{
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

    outputModel->modelData.meshes = std::move(rawData.allMeshes);
    outputModel->modelData.nodes = std::move(rawData.nodes);
    outputModel->modelData.materials = std::move(rawData.materials);

    packedTriangles.reserve(rawData.meshletTriangles.size() / 3);
    for (size_t i = 0; i < rawData.meshletTriangles.size(); i += 3) {
        uint32_t packed = rawData.meshletTriangles[i + 0] |
                          (rawData.meshletTriangles[i + 1] << 8) |
                          (rawData.meshletTriangles[i + 2] << 16);
        packedTriangles.push_back(packed);
    }
}

void ProceduralModelLoadSlot::UploadGeometry(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait)
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

    uploadBuffer(rawData.primitives.data(), rawData.primitives.size(), sizeof(Primitive),
                 resourceManager->primitiveBuffer.handle, outputModel->modelData.primitiveAllocation.offset);

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
                                                  outputModel->modelData.primitiveAllocation.offset, rawData.primitives.size() * sizeof(Primitive)));

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.bufferMemoryBarrierCount = releaseBarriers.size();
    depInfo.pBufferMemoryBarriers = releaseBarriers.data();
    vkCmdPipelineBarrier2(cmd, &depInfo);

    for (auto& barrier : releaseBarriers) {
        outputModel->bufferAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));
    }
}
} // AssetLoad
