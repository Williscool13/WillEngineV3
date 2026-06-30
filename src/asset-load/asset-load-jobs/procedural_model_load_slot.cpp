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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <mutex>

#include "asset-load/asset_load_utils.h"
#include "render/render_utils.h"
#include "core/containers/heap_array.h"
#include "core/memory/memory_manager.h"
#include "par/par_shapes.h"
#include "par/par_shapes_ext.h"
#include "meshoptimizer/src/meshoptimizer.h"
#include "text3d_geometry.h"

namespace AssetLoad
{
ProceduralModelLoadSlot::ProceduralModelLoadSlot() = default;

ProceduralModelLoadSlot::~ProceduralModelLoadSlot() = default;

void ProceduralModelLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> transferDispatchCallback,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
    Core::InlineFunction<void(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestTransferDispatchCallback = std::move(transferDispatchCallback);
    _requestGraphicsDispatchCallback = std::move(graphicsDispatchCallback);
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

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void ProceduralModelLoadSlot::Clear()
{
    slotHandle = {};
    uploadStagingSlotHandle = {};
    outputModel = nullptr;
    uploadStaging = nullptr;

    // RAII dealloc
    rawData = {};
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
        loadSlot->_requestTransferDispatchCallback(cmd, fence, &done);
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
    loadSlot->_requestTransferDispatchCallback(cmd, fence, &done);
    done.acquire();

    vkDestroyFence(loadSlot->context->device, fence, nullptr);
    vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);

    // Build BLAS on graphics queue (requires transfer ownership acquire first)
    {
        VkCommandPoolCreateInfo graphicsPoolInfo = Render::VkHelpers::CommandPoolCreateInfo(loadSlot->context->graphicsQueueFamily);
        VkCommandPool graphicsCommandPool;
        VK_CHECK(vkCreateCommandPool(loadSlot->context->device, &graphicsPoolInfo, nullptr, &graphicsCommandPool));

        VkCommandBufferAllocateInfo graphicsCmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, graphicsCommandPool);
        VkCommandBuffer graphicsCmd;
        VK_CHECK(vkAllocateCommandBuffers(loadSlot->context->device, &graphicsCmdInfo, &graphicsCmd));

        VkFenceCreateInfo graphicsFenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence graphicsFence;
        VK_CHECK(vkCreateFence(loadSlot->context->device, &graphicsFenceInfo, nullptr, &graphicsFence));

        VkCommandBufferBeginInfo graphicsBeginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &graphicsBeginInfo));

        auto graphicsSubmitAndWait = [&](bool reset) {
            ZoneScopedN("GraphicsSubmitAndWait");
            VK_CHECK(vkEndCommandBuffer(graphicsCmd));
            std::binary_semaphore done(0);
            loadSlot->_requestGraphicsDispatchCallback(graphicsCmd, graphicsFence, &done);
            done.acquire();
            if (reset) {
                VK_CHECK(vkResetFences(loadSlot->context->device, 1, &graphicsFence));
                VK_CHECK(vkResetCommandBuffer(graphicsCmd, 0));
                VkCommandBufferBeginInfo restartInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &restartInfo));
            }
        };

        loadSlot->BuildBLAS(graphicsCmd, graphicsSubmitAndWait);

        vkDestroyFence(loadSlot->context->device, graphicsFence, nullptr);
        vkDestroyCommandPool(loadSlot->context->device, graphicsCommandPool, nullptr);
    }

    loadSlot->_notifyCallback(true, loadSlot->slotHandle, loadSlot->uploadStagingSlotHandle);
}

bool ProceduralModelLoadSlot::GenerateGeometry()
{
    ZoneScopedN("GenerateGeometry");

    if (outputModel->text3DParams.has_value()) {
        return GenerateText3D(*outputModel->text3DParams);
    }

    if (outputModel->splineParams.has_value()) {
        return GenerateSpline(*outputModel->splineParams);
    }

    Engine::ProceduralParams& params = outputModel->proceduralParams;

    bool bSuccess = false;
    std::visit(overloaded{
                   [](std::monostate) {},
                   [&](const Engine::StaircaseParams& p) { bSuccess = GenerateStaircase(p); },
                   [&](const Engine::BoxParams& p) { bSuccess = GenerateBox(p); },
                   [&](const Engine::CylinderParams& p) { bSuccess = GenerateCylinder(p); },
                   [&](const Engine::CapsuleParams& p) { bSuccess = GenerateCapsule(p); },
                   [&](const Engine::TorusParams& p) { bSuccess = GenerateTorus(p); },
                   [&](const Engine::ArchParams& p) { bSuccess = GenerateArch(p); },
                   [&](const Engine::WedgeParams& p) { bSuccess = GenerateWedge(p); },
                   [&](const Engine::ConeParams& p) { bSuccess = GenerateCone(p); },
                   [&](const Engine::DoorParams& p) { bSuccess = GenerateDoor(p); },
                   [&](const Engine::PlaneParams& p) { bSuccess = GeneratePlane(p); },
                   [&](const Engine::SphereParams& p) { bSuccess = GenerateSphere(p); },
                   [&](const Engine::SubdividedSphereParams& p) { bSuccess = GenerateSubdividedSphere(p); },
                   [&](const Engine::HemisphereParams& p) { bSuccess = GenerateHemisphere(p); },
                   [&](const Engine::PipeParams& p) { bSuccess = GeneratePipe(p); },
                   [&](const Engine::TetrahedronParams& p) { bSuccess = GenerateTetrahedron(p); },
                   [&](const Engine::OctahedronParams& p) { bSuccess = GenerateOctahedron(p); },
                   [&](const Engine::IcosahedronParams& p) { bSuccess = GenerateIcosahedron(p); },
                   [&](const Engine::DodecahedronParams& p) { bSuccess = GenerateDodecahedron(p); },
                   [&](const Engine::KleinBottleParams& p) { bSuccess = GenerateKleinBottle(p); },
                   [&](const Engine::TrefoilKnotParams& p) { bSuccess = GenerateTrefoilKnot(p); },
                   [&](const Engine::CurvedRampParams& p) { bSuccess = GenerateCurvedRamp(p); },
                   [&](const Engine::BowlParams& p) { bSuccess = GenerateBowl(p); },
                   [&](const Engine::SpiralStaircaseParams& p) { bSuccess = GenerateSpiralStaircase(p); },
               }, params);
    return bSuccess;
}

bool ProceduralModelLoadSlot::GenerateText3D(const Engine::Text3DParams& p)
{
    ZoneScopedN("GenerateText3D");

    if (p.font == nullptr) { return false; }

    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    if (!BuildText3DGeometry(*p.font, p, memoryManager->AssetsScratch(), vertices, indices)) {
        return false;
    }

    return FinalizeGeometry(Core::Span<const Engine::FullVertex>(vertices.Data(), vertices.Size()), Core::Span<const uint32_t>(indices.Data(), indices.Size()));
}

bool ProceduralModelLoadSlot::GenerateStaircase(const Engine::StaircaseParams& p)
{
    ZoneScopedN("GenerateStaircase");

    if (p.stepCount <= 0) return false;

    par_shapes_mesh* merged = par_shapes_create_staircase(p.stepCount, p.width, p.totalDepth, p.totalHeight, p.bSpecifyStepHeight ? p.stepHeight : 0.0f, p.bIsClosed ? 1 : 0);

    if (!merged) { return false; }

    par_shapes_unweld(merged, true);
    par_shapes_compute_normals(merged);

    // Convert to engine vertex format
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, merged->npoints);
    for (int32_t i = 0; i < merged->npoints; ++i) {
        const Vec3 pos = {merged->points[i * 3 + 0], merged->points[i * 3 + 1], merged->points[i * 3 + 2]};
        const Vec3 n = merged->normals ? Vec3(merged->normals[i * 3 + 0], merged->normals[i * 3 + 1], merged->normals[i * 3 + 2]) : Vec3(0, 1, 0);

        // World-space triplanar UV (1 unit = 1 world unit)
        const Vec3 absN = glm::abs(n);
        float u, v;
        if (absN.y >= absN.x && absN.y >= absN.z) {
            u = (n.y > 0.0f) ? -pos.x : pos.x;
            v = pos.z;
        } // tread / bottom
        else if (absN.x >= absN.z) {
            u = (n.x > 0.0f) ? -pos.z : pos.z;
            v = pos.y;
        } // cap faces
        else {
            u = (n.z < 0.0f) ? -pos.x : pos.x;
            v = pos.y;
        } // riser / back wall

        vertices[i].position = pos;
        vertices[i].normal = n;
        vertices[i].uv = {u, v};
        vertices[i].tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, merged->ntriangles * 3);
    for (int32_t i = 0; i < merged->ntriangles * 3; ++i) {
        indices[i] = static_cast<uint32_t>(merged->triangles[i]);
    }

    par_shapes_free_mesh(merged);

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::FinalizeGeometry(Core::Span<const Engine::FullVertex> vertices, Core::Span<const uint32_t> indices)
{
    if (vertices.IsEmpty() || indices.IsEmpty()) return false;

    // Meshoptimizer remap pipeline
    Core::HeapArray<uint32_t> remap(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, vertices.Size());
    size_t uniqueVertices = meshopt_generateVertexRemap(remap.Data(), indices.Data(), indices.Size(), vertices.Data(), vertices.Size(), sizeof(Engine::FullVertex));

    Core::HeapArray<uint32_t> remappedIndices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, indices.Size());
    meshopt_remapIndexBuffer(remappedIndices.Data(), indices.Data(), indices.Size(), remap.Data());

    Core::HeapArray<Engine::FullVertex> remappedVertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, uniqueVertices);
    meshopt_remapVertexBuffer(remappedVertices.Data(), vertices.Data(), vertices.Size(), sizeof(Engine::FullVertex), remap.Data());

    meshopt_optimizeVertexCache(remappedIndices.Data(), remappedIndices.Data(), remappedIndices.Size(), uniqueVertices);
    meshopt_optimizeOverdraw(remappedIndices.Data(), remappedIndices.Data(), remappedIndices.Size(), &remappedVertices[0].position.x, uniqueVertices, sizeof(Engine::FullVertex), 1.05f);
    meshopt_optimizeVertexFetch(remappedVertices.Data(), remappedIndices.Data(), remappedIndices.Size(), remappedVertices.Data(), uniqueVertices, sizeof(Engine::FullVertex));

    // Build meshlets
    size_t maxMeshlets = meshopt_buildMeshletsBound(remappedIndices.Size(), MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES);
    Core::HeapArray<meshopt_Meshlet> meshlets(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, maxMeshlets);
    Core::HeapArray<uint32_t> meshletVertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, maxMeshlets * MESHLET_MAX_VERTICES);
    Core::HeapArray<uint8_t> meshletTriangles(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, maxMeshlets * MESHLET_MAX_TRIANGLES * 3);

    size_t meshletCount = meshopt_buildMeshlets(
        meshlets.Data(), meshletVertices.Data(), meshletTriangles.Data(),
        remappedIndices.Data(), remappedIndices.Size(),
        &remappedVertices[0].position.x, remappedVertices.Size(), sizeof(Engine::FullVertex),
        MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES, 0.0f);

    const meshopt_Meshlet& lastMeshlet = meshlets[meshletCount - 1];
    size_t meshletVertexCount = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
    size_t meshletTriangleCount = lastMeshlet.triangle_offset + lastMeshlet.triangle_count * 3;

    for (size_t i = 0; i < meshletCount; ++i) {
        meshopt_optimizeMeshlet(&meshletVertices[meshlets[i].vertex_offset], &meshletTriangles[meshlets[i].triangle_offset], meshlets[i].triangle_count, meshlets[i].vertex_count);
    }

    // Compute bounds for position quantization and rendering
    Engine::MeshBounds bounds = CalculateMeshBounds(remappedVertices);
    if (bounds.aabbExtents.x < 1e-6f) { bounds.aabbExtents.x = 1.0f; }
    if (bounds.aabbExtents.y < 1e-6f) { bounds.aabbExtents.y = 1.0f; }
    if (bounds.aabbExtents.z < 1e-6f) { bounds.aabbExtents.z = 1.0f; }

    // Fill rawData
    auto vertexOffset = static_cast<uint32_t>(rawData.vertices.Size());
    assert(rawData.vertices.Size() == 0);
    assert(rawData.indices.Size() == 0);
    auto meshletVertexOffset = static_cast<uint32_t>(rawData.meshletVertices.Size());
    auto meshletTriangleOffset = static_cast<uint32_t>(rawData.meshletTriangles.Size());
    auto meshletBaseOffset = static_cast<uint32_t>(rawData.meshlets.Size());

    rawData.vertices = Core::HeapArray<Engine::Vertex>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, remappedVertices.Size());
    rawData.indices = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, remappedIndices.Size());
    rawData.meshletVertices = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, meshletVertexCount);
    rawData.meshletTriangles = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, meshletTriangleCount);
    rawData.meshlets = Core::HeapArray<Meshlet>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, meshletCount);

    for (size_t i = 0; i < remappedVertices.Size(); ++i) {
        rawData.vertices[i] = CompressVertex(remappedVertices[i], bounds);
    }
    for (size_t i = 0; i < remappedIndices.Size(); ++i) {
        rawData.indices[i] = remappedIndices[i];
    }
    for (size_t i = 0; i < meshletVertexCount; ++i) {
        rawData.meshletVertices[i] = meshletVertices[i];
    }
    for (size_t i = 0; i < meshletTriangleCount; ++i) {
        rawData.meshletTriangles[i] = meshletTriangles[i];
    }
    for (size_t i = 0; i < meshletCount; ++i) {
        const meshopt_Meshlet& m = meshlets[i];
        meshopt_Bounds mBounds = meshopt_computeMeshletBounds(
            &meshletVertices[m.vertex_offset], &meshletTriangles[m.triangle_offset], m.triangle_count,
            reinterpret_cast<const float*>(remappedVertices.Data()), remappedVertices.Size(), sizeof(Engine::FullVertex));

        rawData.meshlets[i] = {
            .meshletBoundingSphere = {mBounds.center[0], mBounds.center[1], mBounds.center[2], mBounds.radius},
            .coneApex = {mBounds.cone_apex[0], mBounds.cone_apex[1], mBounds.cone_apex[2]},
            .coneCutoff = mBounds.cone_cutoff,
            .coneAxis = {mBounds.cone_axis[0], mBounds.cone_axis[1], mBounds.cone_axis[2]},
            .vertexOffset = vertexOffset,
            .meshletVertexOffset = meshletVertexOffset + m.vertex_offset,
            .meshletTriangleOffset = meshletTriangleOffset + m.triangle_offset,
            .meshletVertexCount = m.vertex_count,
            .meshletTriangleCount = m.triangle_count,
        };
    }

    rawData.allMeshes = Core::HeapArray<Engine::MeshInformation>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 1);
    rawData.allMeshes[0].name = Core::InlineString<64>(outputModel->name.c_str());
    rawData.allMeshes[0].primitiveProperties.PushBack({static_cast<uint32_t>(rawData.primitives.Size()), -1});

    rawData.primitives = Core::HeapArray<Primitive>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 1);
    rawData.primitives[0].meshletOffset = IVec4(static_cast<int>(meshletBaseOffset));
    rawData.primitives[0].meshletCount = IVec4(static_cast<int>(meshletCount));
    rawData.primitives[0].boundingSphere = {bounds.sphere.center, bounds.sphere.radius};
    rawData.primitives[0].boundingBoxMin = bounds.aabb.min;
    rawData.primitives[0].boundingBoxMax = bounds.aabb.max;
    rawData.primitives[0].bHasTransparent = 0;
    rawData.primitives[0].indexOffset = 0;

    // Procedural models strictly do not come with a material.
    // rawData.materials = Core::HeapArray<Primitive>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, materials.size());

    {
        outputModel->physicsCache = PhysicsCache{};
        auto& physicsCache = outputModel->physicsCache.value();

        // Positions are available directly from remappedVertices
        Core::HeapArray<Vec3> allPositions(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, remappedVertices.Size());
        for (size_t i = 0; i < remappedVertices.Size(); ++i) {
            allPositions[i] = remappedVertices[i].position;
        }

        Core::HeapArray<uint32_t> allIndices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, remappedIndices.Size());
        for (size_t i = 0; i < remappedIndices.Size(); ++i) {
            allIndices[i] = remappedIndices[i];
        }

        // Always need to compute bounds here for procedural; compute with full fidelity index buffer
        outputModel->bounds = ComputeBounds(allPositions);

        // todo parameterize
        constexpr size_t kSimplifyThreshold = 1500;
        constexpr size_t kSimplifyFloor = 1500;
        constexpr float kSimplifyRatio = 0.15f;
        constexpr float kSimplifyError = 0.01f;
        if (allIndices.Size() > kSimplifyThreshold) {
            const size_t target = std::max(kSimplifyFloor, static_cast<size_t>(allIndices.Size() * kSimplifyRatio));
            Core::HeapArray<uint32_t> simplified(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, allIndices.Size());
            const size_t simplifiedCount = meshopt_simplify(
                simplified.Data(),
                allIndices.Data(), allIndices.Size(),
                &allPositions[0].x, allPositions.Size(), sizeof(Vec3),
                target, kSimplifyError
            );

            Core::HeapArray<uint32_t> remap(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, allPositions.Size());
            const size_t uniqueVerts = meshopt_generateVertexRemap(remap.Data(), simplified.Data(), simplifiedCount, allPositions.Data(), allPositions.Size(), sizeof(Vec3));

            physicsCache.positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::AssetModel, uniqueVerts);
            meshopt_remapVertexBuffer(physicsCache.positions.Data(), allPositions.Data(), allPositions.Size(), sizeof(Vec3), remap.Data());

            physicsCache.indices = Core::HeapArray<uint32_t>(&memoryManager->Assets(), Core::AllocTag::AssetModel, simplifiedCount);
            meshopt_remapIndexBuffer(physicsCache.indices.Data(), simplified.Data(), simplifiedCount, remap.Data());
        } else {
            physicsCache.positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::AssetModel, allPositions.Size());
            for (size_t i = 0; i < allPositions.Size(); ++i) {
                physicsCache.positions[i] = allPositions[i];
            }

            physicsCache.indices = Core::HeapArray<uint32_t>(&memoryManager->Assets(), Core::AllocTag::AssetModel, allIndices.Size());
            for (size_t i = 0; i < allIndices.Size(); ++i) {
                physicsCache.indices[i] = allIndices[i];
            }
        }
    }

    return true;
}

bool ProceduralModelLoadSlot::GenerateBox(const Engine::BoxParams& p)
{
    ZoneScopedN("GenerateBox");

    const float sx = p.sizeX, sy = p.sizeY, sz = p.sizeZ;

    // 6 quads × 4 verts = 24, 6 quads × 6 indices = 36
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 24);
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 36);
    size_t vi = 0, ii = 0;

    auto addFace = [&](Vec3 n, Vec3 t,
                       Vec3 v0, Vec3 v1, Vec3 v2, Vec3 v3,
                       Vec2 uv0, Vec2 uv1, Vec2 uv2, Vec2 uv3) {
        auto base = static_cast<uint32_t>(vi);
        auto push = [&](Vec3 pos, Vec2 uv) {
            Engine::FullVertex v{};
            v.position = pos;
            v.normal = n;
            v.uv = { uv.x, uv.y};
            v.tangent = {t.x, t.y, t.z, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices[vi++] = v;
        };
        push(v0, uv0);
        push(v1, uv1);
        push(v2, uv2);
        push(v3, uv3);
        indices[ii++] = base; indices[ii++] = base + 1; indices[ii++] = base + 2;
        indices[ii++] = base; indices[ii++] = base + 2; indices[ii++] = base + 3;
    };

    // Corner pivot: origin at (0,0,0), box extends to (sizeX, sizeY, sizeZ).
    // Vertex order per face is chosen so (v1-v0)×(v2-v0) points outward (CCW from outside).
    // UV: world-space (1 UV unit = 1 world unit).

    // +X: normal=(1,0,0), U=Z(flipped), V=Y
    addFace({1, 0, 0}, {0, 0, -1},
            {sx, 0, 0}, {sx, sy, 0}, {sx, sy, sz}, {sx, 0, sz},
            {sz, 0}, {sz, sy}, {0, sy}, {0, 0});
    // -X: normal=(-1,0,0), U=Z(mirrored), V=Y
    addFace({-1, 0, 0}, {0, 0, -1},
            {0, 0, sz}, {0, sy, sz}, {0, sy, 0}, {0, 0, 0},
            {sz, 0}, {sz, sy}, {0, sy}, {0, 0});
    // +Y: normal=(0,1,0), U=X(flipped), V=Z
    addFace({0, 1, 0}, {-1, 0, 0},
            {0, sy, 0}, {0, sy, sz}, {sx, sy, sz}, {sx, sy, 0},
            {sx, 0}, {sx, sz}, {0, sz}, {0, 0});
    // -Y: normal=(0,-1,0), U=X, V=Z
    addFace({0, -1, 0}, {1, 0, 0},
            {0, 0, 0}, {sx, 0, 0}, {sx, 0, sz}, {0, 0, sz},
            {0, 0}, {sx, 0}, {sx, sz}, {0, sz});
    // +Z: normal=(0,0,1), U=X, V=Y
    addFace({0, 0, 1}, {1, 0, 0},
            {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz},
            {0, 0}, {sx, 0}, {sx, sy}, {0, sy});
    // -Z: normal=(0,0,-1), U=X, V=Y
    addFace({0, 0, -1}, {1, 0, 0},
            {0, 0, 0}, {0, sy, 0}, {sx, sy, 0}, {sx, 0, 0},
            {sx, 0}, {sx, sy}, {0, sy}, {0, 0});

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateCylinder(const Engine::CylinderParams& p)
{
    ZoneScopedN("GenerateCylinder");

    const int N = std::max(3, p.slices);
    const float r = p.radius;
    const float h = p.height;
    const float hh = h * 0.5f;

    // par_shapes cylinder: Z-up, Z=0 bottom, Z=1 top, radius=1, no caps, smooth normals.
    // Remap to Y-up (proper rotation, winding preserved): engine = (par.x*r, par.z*h-hh, -par.y*r)
    par_shapes_mesh* m = par_shapes_create_cylinder(N, 1);
    if (!m) return false;

    const int baseVerts = m->npoints;
    const int baseIdxs = m->ntriangles * 3;
    const int capVerts = p.bCapped ? (N + 1) * 2 : 0;
    const int capIdxs = p.bCapped ? N * 3 * 2 : 0;

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, baseVerts + capVerts);
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, baseIdxs + capIdxs);

    for (int i = 0; i < baseVerts; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        vertices[i].position = {px * r, pz * h - hh, -py * r};
        const float nx = m->normals[i * 3 + 0], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        vertices[i].normal = glm::normalize(Vec3{nx, nz, -ny});
        vertices[i].uv = {m->tcoords[i * 2 + 0], m->tcoords[i * 2 + 1]}; // u = height 0→1, v = angle 0→1
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    for (int i = 0; i < baseIdxs; ++i) {
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
    }
    par_shapes_free_mesh(m);

    if (p.bCapped) {
        size_t vi = baseVerts;
        size_t ii = baseIdxs;

        // Top cap (+Y): {center, ring[j+1], ring[j]} → +Y ✓
        auto capBase = static_cast<uint32_t>(vi);
        Engine::FullVertex cv{};
        cv.position = {0, hh, 0};
        cv.normal = {0, 1, 0};
        cv.uv = {0.5f, 0.5f};
        cv.tangent = {-1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices[vi++] = cv;
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * glm::pi<float>();
            Engine::FullVertex v{};
            v.position = {cosf(angle) * r, hh, sinf(angle) * r};
            v.normal = {0, 1, 0};
            v.uv = {-cosf(angle) * 0.5f + 0.5f, sinf(angle) * 0.5f + 0.5f};
            v.tangent = {-1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices[vi++] = v;
        }
        for (int j = 0; j < N; j++) {
            indices[ii++] = capBase;
            indices[ii++] = capBase + 1 + (j + 1) % N;
            indices[ii++] = capBase + 1 + j;
        }

        // Bottom cap (-Y): {center, ring[j], ring[j+1]} → -Y ✓
        capBase = static_cast<uint32_t>(vi);
        cv.position = {0, -hh, 0};
        cv.normal = {0, -1, 0};
        vertices[vi++] = cv;
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * glm::pi<float>();
            Engine::FullVertex v{};
            v.position = {cosf(angle) * r, -hh, sinf(angle) * r};
            v.normal = {0, -1, 0};
            v.uv = {cosf(angle) * 0.5f + 0.5f, sinf(angle) * 0.5f + 0.5f};
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices[vi++] = v;
        }
        for (int j = 0; j < N; j++) {
            indices[ii++] = capBase;
            indices[ii++] = capBase + 1 + j;
            indices[ii++] = capBase + 1 + (j + 1) % N;
        }
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateCapsule(const Engine::CapsuleParams& p)
{
    ZoneScopedN("GenerateCapsule");

    const int N = std::max(3, p.slices);
    const int HR = std::max(2, p.rings);
    const float r = p.radius;
    const float bhh = std::max(0.0f, p.height * 0.5f - r); // half body height

    // verts: 2 poles + 2*HR*(N+1) rings; indices: 6*N*(2*HR-1)
    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 2 + 2 * HR * (N + 1));
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 6 * N * (2 * HR - 1));

    const auto pi = glm::pi<float>();
    const float pi2 = 2.0f * pi;

    // Helper: add a ring of (N+1) vertices at given y, ringR, outward normal y-component
    auto addRing = [&](float ringY, float ringR, float nY, float nXZScale) {
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * pi2;
            const float cx = cosf(angle), cz = sinf(angle);
            Engine::FullVertex v{};
            v.position = {cx * ringR, ringY, cz * ringR};
            v.normal = {cx * nXZScale, nY, cz * nXZScale};
            v.uv = {1.0f - static_cast<float>(j) / static_cast<float>(N), (ringY + bhh + r) / (2.0f * (bhh + r))};
            v.tangent = {cz, 0, -cx, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        }
    };

    // Connect two adjacent rings (each has N+1 verts starting at baseA / baseB)
    auto connectRings = [&](uint32_t baseA, uint32_t baseB) {
        for (int j = 0; j < N; j++) {
            uint32_t a0 = baseA + j, a1 = baseA + j + 1;
            uint32_t b0 = baseB + j, b1 = baseB + j + 1;
            const uint32_t quad[] = {a0, a1, b0, b0, a1, b1};
            indices.Append(quad, quad + 6);
        }
    };

    // Top pole
    {
        Engine::FullVertex v{};
        v.position = {0, bhh + r, 0};
        v.normal = {0, 1, 0};
        v.uv = {0.5f, 1.0f};
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.PushBack(v);
    }
    const uint32_t topPole = 0;

    // Top hemisphere rings (ring 1 = near pole, ring HR = equator)
    Core::HeapArray<uint32_t> topRingBase(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, HR + 1);
    for (int i = 1; i <= HR; i++) {
        const float phi = pi * 0.5f * static_cast<float>(i) / static_cast<float>(HR); // 0→pi/2
        topRingBase[i] = static_cast<uint32_t>(vertices.Size());
        addRing(r * cosf(phi) + bhh, r * sinf(phi), cosf(phi), sinf(phi));
    }

    // Bottom hemisphere rings (ring 1 = equator, ring HR = near pole)
    Core::HeapArray<uint32_t> botRingBase(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, HR + 1);
    for (int i = 1; i <= HR; i++) {
        const float psi = pi * 0.5f * static_cast<float>(i) / static_cast<float>(HR); // 0→pi/2
        botRingBase[i] = static_cast<uint32_t>(vertices.Size());
        addRing(-r * sinf(psi) - bhh, r * cosf(psi), -sinf(psi), cosf(psi));
    }

    // Bottom pole
    const auto botPole = static_cast<uint32_t>(vertices.Size()); {
        Engine::FullVertex v{};
        v.position = {0, -(bhh + r), 0};
        v.normal = {0, -1, 0};
        v.uv = {0.5f, 0.0f};
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.PushBack(v);
    }

    // Top pole fan → first top ring
    for (int j = 0; j < N; j++) {
        const uint32_t tri[] = {topPole, topRingBase[1] + j + 1, topRingBase[1] + j};
        indices.Append(tri, tri + 3);
    }

    // Top hemisphere strips
    for (int i = 1; i < HR; i++) {
        connectRings(topRingBase[i], topRingBase[i + 1]);
    }

    // Body strip (top equator → bottom equator)
    connectRings(topRingBase[HR], botRingBase[1]);

    // Bottom hemisphere strips (stop at HR-1: ring[HR] has ringR=0 and is degenerate)
    for (int i = 1; i < HR - 1; i++) {
        connectRings(botRingBase[i], botRingBase[i + 1]);
    }

    // Last non-degenerate bottom ring → bottom pole fan
    const uint32_t lastBot = botRingBase[HR - 1];
    for (int j = 0; j < N; j++) {
        const uint32_t tri[] = {botPole, lastBot + j, lastBot + j + 1};
        indices.Append(tri, tri + 3);
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateTorus(const Engine::TorusParams& p)
{
    ZoneScopedN("GenerateTorus");

    const int slices = std::max(3, p.slices);
    const int stacks = std::max(3, p.stacks);
    const float rr = std::max(0.01f, p.ringRadius);
    const float tr = std::max(0.001f, p.tubeRadius);

    // par_shapes asserts ratio in [0.1, 1.0] — clamp as safety net
    const float ratio = glm::clamp(tr / rr, 0.1f, 0.99f);
    par_shapes_mesh* m = par_shapes_create_torus(slices, stacks, ratio);
    if (!m) return false;

    // Scale by ring radius
    for (int i = 0; i < m->npoints; ++i) {
        m->points[i * 3 + 0] *= rr;
        m->points[i * 3 + 1] *= rr;
        m->points[i * 3 + 2] *= rr;
    }
    if (m->normals) {
        // par_shapes generates unit normals — no rescaling needed
    }

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        vertices[i].position = {m->points[i * 3 + 0], m->points[i * 3 + 1], m->points[i * 3 + 2]};
        vertices[i].normal = m->normals ? Vec3(m->normals[i * 3 + 0], m->normals[i * 3 + 1], m->normals[i * 3 + 2]) : Vec3(0, 1, 0);
        vertices[i].uv = {m->tcoords ? m->tcoords[i * 2 + 0] : 0.0f, m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) {
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
    }

    par_shapes_free_mesh(m);

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateArch(const Engine::ArchParams& p)
{
    ZoneScopedN("GenerateArch");

    const int N = std::max(1, p.sides);
    const float outerR = p.width * 0.5f;
    const float innerR = std::max(0.01f, outerR - p.thickness);
    const float legH = std::max(0.0f, p.height - outerR);
    const float depth = p.depth;
    const auto pi = glm::pi<float>();

    // Build outer and inner arc contour points (in XY)
    // N=1 special case: rectangular top (3 segments) instead of degenerate zero-height arc.
    // The rectangle extends up by outerR to match the visual "depth" of the arch sides.
    const int arcSize = (N == 1) ? 4 : (N + 1);
    Core::HeapArray<Vec2> outerArc(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, arcSize);
    Core::HeapArray<Vec2> innerArc(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, arcSize);
    if (N == 1) {
        outerArc[0] = {outerR, legH}; outerArc[1] = {outerR, legH + outerR};
        outerArc[2] = {-outerR, legH + outerR}; outerArc[3] = {-outerR, legH};
        innerArc[0] = {innerR, legH}; innerArc[1] = {innerR, legH + innerR};
        innerArc[2] = {-innerR, legH + innerR}; innerArc[3] = {-innerR, legH};
    }
    else {
        for (int i = 0; i <= N; i++) {
            const float theta = pi * static_cast<float>(i) / static_cast<float>(N);
            outerArc[i] = {outerR * cosf(theta), legH + outerR * sinf(theta)};
            innerArc[i] = {innerR * cosf(theta), legH + innerR * sinf(theta)};
        }
    }

    // Outer contour: bottom-right, arc points, bottom-left
    // Inner contour: same structure
    const int cSize = arcSize + 2;
    Core::HeapArray<Vec2> outerC(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, cSize);
    Core::HeapArray<Vec2> innerC(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, cSize);
    outerC[0] = {outerR, 0};
    for (int i = 0; i < arcSize; i++) { outerC[i + 1] = outerArc[i]; }
    outerC[cSize - 1] = {-outerR, 0};
    innerC[0] = {innerR, 0};
    for (int i = 0; i < arcSize; i++) { innerC[i + 1] = innerArc[i]; }
    innerC[cSize - 1] = {-innerR, 0};

    const int M = cSize;

    // 4 sets of (M-1) quads + 2 bottom cap quads; each quad: 4 verts, 6 indices
    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 16 * M - 8);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 24 * M - 12);

    // Helper: add a flat quad with given normal and basic UV
    auto addQuad = [&](Vec3 v0, Vec3 v1, Vec3 v2, Vec3 v3, Vec3 n) {
        auto base = static_cast<uint32_t>(vertices.Size());
        const Vec3 absN = glm::abs(n);
        const bool flipU = (absN.x >= absN.y && absN.x >= absN.z && n.x > 0.0f)
                           || (absN.y >= absN.x && absN.y >= absN.z && n.y > 0.0f)
                           || (absN.z >= absN.x && absN.z >= absN.y && n.z < 0.0f);
        auto push = [&](Vec3 pos) {
            Engine::FullVertex v{};
            v.position = pos;
            v.normal = n;
            v.uv = {flipU ? -pos.x : pos.x, pos.y};
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        };
        push(v0);
        push(v1);
        push(v2);
        push(v3);
        const uint32_t quad[] = {base, base + 1, base + 2, base, base + 2, base + 3};
        indices.Append(quad, quad + 6);
    };

    // Front face (z=0, normal (0,0,-1)): pair outer and inner contour segments
    for (int i = 0; i < M - 1; i++) {
        addQuad(
            {outerC[i].x, outerC[i].y, 0},
            {innerC[i].x, innerC[i].y, 0},
            {innerC[i + 1].x, innerC[i + 1].y, 0},
            {outerC[i + 1].x, outerC[i + 1].y, 0},
            {0, 0, -1});
    }

    // Back face (z=depth, normal (0,0,1)): reversed winding
    for (int i = 0; i < M - 1; i++) {
        addQuad(
            {outerC[i + 1].x, outerC[i + 1].y, depth},
            {innerC[i + 1].x, innerC[i + 1].y, depth},
            {innerC[i].x, innerC[i].y, depth},
            {outerC[i].x, outerC[i].y, depth},
            {0, 0, 1});
    }

    // Smooth-normal quad: per-vertex normals at each end (nA at A, nB at B)
    auto addSmoothWallQuad = [&](Vec2 A, Vec2 B, Vec3 nA, Vec3 nB, bool reversed) {
        auto base = static_cast<uint32_t>(vertices.Size());
        auto push = [&](Vec3 pos, Vec3 n) {
            Engine::FullVertex v{};
            v.position = pos;
            v.normal = n;
            v.uv = {pos.x, pos.y};
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        };
        if (!reversed) {
            push({A.x, A.y, 0}, nA);
            push({B.x, B.y, 0}, nB);
            push({B.x, B.y, depth}, nB);
            push({A.x, A.y, depth}, nA);
        }
        else {
            push({B.x, B.y, 0}, nB);
            push({A.x, A.y, 0}, nA);
            push({A.x, A.y, depth}, nA);
            push({B.x, B.y, depth}, nB);
        }
        const uint32_t quad[] = {base, base + 1, base + 2, base, base + 2, base + 3};
        indices.Append(quad, quad + 6);
    };

    // Outer side walls: arc segments use smooth radial normals from (0, legH);
    // legs and rectangular top use flat edge-perpendicular normals.
    for (int i = 0; i < M - 1; i++) {
        const Vec2 A = outerC[i], B = outerC[i + 1];
        const bool isArc = (N >= 2) && (i > 0) && (i < M - 2);
        if (isArc) {
            const Vec3 nA = glm::normalize(Vec3{A.x, A.y - legH, 0.0f});
            const Vec3 nB = glm::normalize(Vec3{B.x, B.y - legH, 0.0f});
            addSmoothWallQuad(A, B, nA, nB, false);
        }
        else {
            const Vec2 d = B - A;
            addQuad({A.x, A.y, 0}, {B.x, B.y, 0}, {B.x, B.y, depth}, {A.x, A.y, depth},
                    glm::normalize(Vec3{d.y, -d.x, 0.0f}));
        }
    }

    // Inner side walls: same but inward normals, reversed winding
    for (int i = 0; i < M - 1; i++) {
        const Vec2 A = innerC[i], B = innerC[i + 1];
        const bool isArc = (N >= 2) && (i > 0) && (i < M - 2);
        if (isArc) {
            const Vec3 nA = glm::normalize(Vec3{-A.x, legH - A.y, 0.0f});
            const Vec3 nB = glm::normalize(Vec3{-B.x, legH - B.y, 0.0f});
            addSmoothWallQuad(A, B, nA, nB, true);
        }
        else {
            const Vec2 d = B - A;
            addQuad({B.x, B.y, 0}, {A.x, A.y, 0}, {A.x, A.y, depth}, {B.x, B.y, depth},
                    glm::normalize(Vec3{-d.y, d.x, 0.0f}));
        }
    }

    // Bottom caps (y=0): right and left rectangles connecting outer to inner
    // Right: from (innerR, 0) to (outerR, 0)
    addQuad({innerR, 0, 0}, {outerR, 0, 0}, {outerR, 0, depth}, {innerR, 0, depth}, {0, -1, 0});
    // Left: from (-outerR, 0) to (-innerR, 0)
    addQuad({-outerR, 0, 0}, {-innerR, 0, 0}, {-innerR, 0, depth}, {-outerR, 0, depth}, {0, -1, 0});

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateWedge(const Engine::WedgeParams& p)
{
    ZoneScopedN("GenerateWedge");

    const float sx = p.sizeX, sy = p.sizeY, sz = p.sizeZ;
    if (sx <= 0.0f || sy <= 0.0f || sz <= 0.0f) return false;

    // 3 quads × 4 verts + 2 tris × 3 verts = 18; 3 × 6 + 2 × 3 = 24 indices
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 18);
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 24);
    size_t vi = 0, ii = 0;

    auto addQuad = [&](Vec3 n, Vec3 t,
                       Vec3 v0, Vec3 v1, Vec3 v2, Vec3 v3,
                       Vec2 uv0, Vec2 uv1, Vec2 uv2, Vec2 uv3) {
        auto base = static_cast<uint32_t>(vi);
        auto push = [&](Vec3 pos, Vec2 uv) {
            Engine::FullVertex v{};
            v.position = pos;
            v.normal = n;
            v.uv = uv;
            v.tangent = {t.x, t.y, t.z, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices[vi++] = v;
        };
        push(v0, uv0);
        push(v1, uv1);
        push(v2, uv2);
        push(v3, uv3);
        indices[ii++] = base; indices[ii++] = base + 1; indices[ii++] = base + 2;
        indices[ii++] = base; indices[ii++] = base + 2; indices[ii++] = base + 3;
    };

    auto addTri = [&](Vec3 n, Vec3 t,
                      Vec3 v0, Vec3 v1, Vec3 v2,
                      Vec2 uv0, Vec2 uv1, Vec2 uv2) {
        auto base = static_cast<uint32_t>(vi);
        auto push = [&](Vec3 pos, Vec2 uv) {
            Engine::FullVertex v{};
            v.position = pos;
            v.normal = n;
            v.uv = uv;
            v.tangent = {t.x, t.y, t.z, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices[vi++] = v;
        };
        push(v0, uv0);
        push(v1, uv1);
        push(v2, uv2);
        indices[ii++] = base; indices[ii++] = base + 1; indices[ii++] = base + 2;
    };

    // Corner pivot at (0,0,0): bottom-front edge at Z=0, back-top edge at (x, sy, sz).
    // Winding: CCW from outside. Index pattern {0,1,2, 0,2,3} verified per face.

    // Bottom (-Y): (v1-v0)×(v2-v0) = (sx,0,0)×(sx,0,sz) → (0,-sx*sz,0) → -Y ✓
    addQuad({0, -1, 0}, {1, 0, 0},
            {0, 0, 0}, {sx, 0, 0}, {sx, 0, sz}, {0, 0, sz},
            {0, 0}, {sx, 0}, {sx, sz}, {0, sz});

    // Back (+Z): (v1-v0)×(v2-v0) = (sx,0,0)×(sx,sy,0) → (0,0,sx*sy) → +Z ✓
    addQuad({0, 0, 1}, {1, 0, 0},
            {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz},
            {0, 0}, {sx, 0}, {sx, sy}, {0, sy});

    // Slope: outward normal = (0, sz, -sy)/len (away from wedge interior)
    // (v1-v0)×(v2-v0) = (0,sy,sz)×(sx,sy,sz) → (0, sz*sx, -sy*sx) → (0,sz,-sy) ✓
    const float slopeLen = sqrtf(sy * sy + sz * sz);
    const Vec3 slopeN = glm::normalize(Vec3(0.0f, sz, -sy));
    addQuad(slopeN, {1, 0, 0},
            {0, 0, 0}, {0, sy, sz}, {sx, sy, sz}, {sx, 0, 0},
            {sx, 0}, {sx, slopeLen}, {0, slopeLen}, {0, 0});

    // Left cap (-X): (v1-v0)×(v2-v0) = (0,0,sz)×(0,sy,sz) → (-sz*sy,0,0) → -X ✓
    addTri({-1, 0, 0}, {0, 0, 1},
           {0, 0, 0}, {0, 0, sz}, {0, sy, sz},
           {0, 0}, {sz, 0}, {sz, sy});

    // Right cap (+X): (v1-v0)×(v2-v0) = (0,sy,sz)×(0,0,sz) → (sy*sz,0,0) → +X ✓
    addTri({1, 0, 0}, {0, 0, 1},
           {sx, 0, 0}, {sx, sy, sz}, {sx, 0, sz},
           {sz, 0}, {0, sy}, {0, 0});

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateCone(const Engine::ConeParams& p)
{
    ZoneScopedN("GenerateCone");

    const int N = std::max(3, p.slices);
    const float r = p.radius;
    const float h = p.height;

    // par_shapes cone: Z-up, Z=0 base (radius=1), Z=1 apex (radius=0), no cap.
    // Smooth lateral normal: n = normalize(h*cx, r, h*cz) where (cx,cz) is the XZ unit direction.
    par_shapes_mesh* m = par_shapes_create_cone(N, 1);
    if (!m) return false;

    const int baseVerts = m->npoints;
    const int baseIdxs = m->ntriangles * 3;
    const int capVerts = p.bCapped ? (N + 1) : 0;
    const int capIdxs = p.bCapped ? N * 3 : 0;

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, baseVerts + capVerts);
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, baseIdxs + capIdxs);

    // Remap to Y-up: engine = (par.x*r, par.z*h, -par.y*r)
    for (int i = 0; i < baseVerts; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const Vec3 pos = {px * r, pz * h, -py * r};
        vertices[i].position = pos;
        const float xzRadius = sqrtf(pos.x * pos.x + pos.z * pos.z);
        if (xzRadius > 1e-6f) {
            vertices[i].normal = glm::normalize(Vec3{h * pos.x / xzRadius, r, h * pos.z / xzRadius});
        }
        else {
            vertices[i].normal = {0.0f, 1.0f, 0.0f}; // apex
        }
        vertices[i].uv = {atan2f(-pos.z, pos.x) / (2.0f * glm::pi<float>()) + 0.5f, pos.y / h};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    for (int i = 0; i < baseIdxs; ++i) {
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
    }
    par_shapes_free_mesh(m);

    if (p.bCapped) {
        size_t vi = baseVerts;
        size_t ii = baseIdxs;

        // Bottom cap (-Y): {center, ring[j], ring[j+1]} → -Y ✓
        auto capBase = static_cast<uint32_t>(vi);
        Engine::FullVertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.uv = {0.5f, 0.5f};
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices[vi++] = cv;
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * glm::pi<float>();
            Engine::FullVertex v{};
            v.position = {cosf(angle) * r, 0.0f, sinf(angle) * r};
            v.normal = {0, -1, 0};
            v.uv = {cosf(angle) * 0.5f + 0.5f, sinf(angle) * 0.5f + 0.5f};
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices[vi++] = v;
        }
        for (int j = 0; j < N; j++) {
            indices[ii++] = capBase;
            indices[ii++] = capBase + 1 + j;
            indices[ii++] = capBase + 1 + (j + 1) % N;
        }
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateDoor(const Engine::DoorParams& p)
{
    ZoneScopedN("GenerateDoor");

    const float w = p.width;
    const float h = p.height;
    const float d = p.depth;
    const float hA = p.archHeight;
    const int sides = std::max(2, p.sides);

    if (w <= 0.0f || h <= 0.0f || d <= 0.0f || hA < 0.0f || hA >= h) return false;

    const float archStart = h - hA; // Y where the rectangular leg ends

    // Compute arc parameters (general circular arc spanning width w with height hA).
    // Circle: center = (w/2, archStart - c), radius = hA + c, where c = (w²/4 - hA²)/(2·hA).
    // hA=0 → flat top (special-cased). hA=w/2 → semicircle (c=0). hA>w/2 → Gothic arch (c<0).
    float arcC = 0.0f, arcR = 0.0f, thetaStart = 0.0f, thetaEnd = glm::pi<float>();
    Vec2 arcCen{w * 0.5f, archStart};
    if (hA > 0.0f) {
        arcC = (w * w * 0.25f - hA * hA) / (2.0f * hA);
        arcCen = {w * 0.5f, archStart - arcC};
        arcR = hA + arcC;
        thetaStart = atan2f(arcC, w * 0.5f); // angle from center to (w, archStart)
        thetaEnd = glm::pi<float>() - thetaStart; // angle to (0, archStart)
    }

    // Build 2D profile polygon, CCW in XY.
    // Profile always closes: last vertex → (0,0) via the left (hinge) edge.
    // poly size: 2 base + arc(sides+1) or 2 flat points
    const int polySize = (hA > 0.0f) ? (sides + 3) : 4;
    Core::HeapArray<Vec2> poly(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, polySize);
    size_t pi_ = 0;

    poly[pi_++] = {0.0f, 0.0f}; // bottom-left (hinge)

    const float seamX = p.bHalf ? w * 0.5f - p.gap : w;
    poly[pi_++] = {seamX, 0.0f}; // bottom-right / bottom-seam

    if (hA > 0.0f) {
        // Arc: full leaf θ=thetaStart→thetaEnd; half leaf θ from x=seamX→thetaEnd
        // arcFrom = acos(-gap/arcR) keeps the seam edge vertical (gap=0 → π/2 as before)
        const float arcFrom = p.bHalf ? acosf(glm::clamp(-p.gap / arcR, -1.0f, 1.0f)) : thetaStart;
        for (int i = 0; i <= sides; i++) {
            const float t = static_cast<float>(i) / static_cast<float>(sides);
            const float theta = arcFrom + (thetaEnd - arcFrom) * t;
            poly[pi_++] = {arcCen.x + arcR * cosf(theta), arcCen.y + arcR * sinf(theta)};
        }
        // poly.back() = (0, archStart); closing edge returns to (0,0)
    }
    else {
        // Flat top
        poly[pi_++] = {seamX, h};
        poly[pi_++] = {0.0f, h};
    }

    // bFlip: mirror about x=0 (door extends to -X) and reverse poly[1..] to maintain CCW
    if (p.bFlip) {
        for (auto& pt : poly) { pt.x = -pt.x; }
        std::reverse(poly.begin() + 1, poly.end());
    }

    const int M = static_cast<int>(poly.Size());

    // vertices: front M + back M + perimeter M*4; indices: front (M-2)*3 + back (M-2)*3 + perimeter M*6
    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, M * 6);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, (M - 2) * 6 + M * 6);

    // Front face (Z=0, normal (0,0,-1)): fan with reversed winding so cross product = -Z
    {
        auto base = static_cast<uint32_t>(vertices.Size());
        for (const auto& pt : poly) {
            Engine::FullVertex v{};
            v.position = {pt.x, pt.y, 0.0f};
            v.normal = {0, 0, -1};
            v.uv = {-pt.x, pt.y};
            v.tangent = {-1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        }
        for (int i = 1; i + 1 < M; i++) {
            const uint32_t tri[] = {base, base + (uint32_t)(i + 1), base + (uint32_t)i};
            indices.Append(tri, tri + 3);
        }
    }

    // Back face (Z=d, normal (0,0,1)): forward winding so cross product = +Z
    {
        auto base = static_cast<uint32_t>(vertices.Size());
        for (const auto& pt : poly) {
            Engine::FullVertex v{};
            v.position = {pt.x, pt.y, d};
            v.normal = {0, 0, 1};
            v.uv = {pt.x, pt.y};
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        }
        for (int i = 1; i + 1 < M; i++) {
            const uint32_t tri[] = {base, base + (uint32_t)i, base + (uint32_t)(i + 1)};
            indices.Append(tri, tri + 3);
        }
    }

    // Arc center may be mirrored by bFlip
    const Vec2 effectiveArcCen = p.bFlip ? Vec2{-arcCen.x, arcCen.y} : arcCen;

    // Returns true if pt lies on the arc (within tolerance)
    auto isOnArc = [&](Vec2 pt) -> bool {
        if (hA <= 0.0f || arcR < 1e-6f) return false;
        return fabsf(glm::length(pt - effectiveArcCen) - arcR) < 1e-4f;
    };

    // Perimeter walls: flat edges use edge-perpendicular normal; arc edges use smooth radial normals.
    for (int i = 0; i < M; i++) {
        const Vec2 A = poly[i];
        const Vec2 B = poly[(i + 1) % M];
        const float edgeLen = glm::length(B - A);
        if (edgeLen < 1e-6f) { continue; }

        Vec3 nA, nB;
        if (isOnArc(A) && isOnArc(B)) {
            nA = glm::normalize(Vec3{A.x - effectiveArcCen.x, A.y - effectiveArcCen.y, 0.0f});
            nB = glm::normalize(Vec3{B.x - effectiveArcCen.x, B.y - effectiveArcCen.y, 0.0f});
        }
        else {
            nA = nB = glm::normalize(Vec3{B.y - A.y, A.x - B.x, 0.0f});
        }

        auto base = static_cast<uint32_t>(vertices.Size());
        auto push = [&](Vec3 pos, Vec3 n, Vec2 uv) {
            Engine::FullVertex v{};
            v.position = pos;
            v.normal = n;
            v.uv = uv;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        };
        push({A.x, A.y, 0.0f}, nA, {0.0f, 0.0f});
        push({B.x, B.y, 0.0f}, nB, {edgeLen, 0.0f});
        push({B.x, B.y, d}, nB, {edgeLen, d});
        push({A.x, A.y, d}, nA, {0.0f, d});
        const uint32_t quad[] = {base, base + 1, base + 2, base, base + 2, base + 3};
        indices.Append(quad, quad + 6);
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GeneratePlane(const Engine::PlaneParams& p)
{
    ZoneScopedN("GeneratePlane");

    const int tx = std::max(1, p.tilesX);
    const int tz = std::max(1, p.tilesZ);
    const float sx = p.sizeX;
    const float sz = p.sizeZ;

    // par_shapes plane: XY plane, (0,0)→(1,1), normal +Z.
    // Remap to engine XZ plane (Y=0, normal +Y): engine = ((par.x-0.5)*sx, 0, (par.y-0.5)*sz)
    par_shapes_mesh* m = par_shapes_create_plane(tx, tz);
    if (!m) return false;

    par_shapes_compute_normals(m);

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1];
        vertices[i].position = {(px - 0.5f) * sx, 0.0f, (py - 0.5f) * sz};
        vertices[i].normal = {0.0f, 1.0f, 0.0f};
        vertices[i].uv = {m->tcoords ? m->tcoords[i * 2 + 0] * sx : px * sx, m->tcoords ? m->tcoords[i * 2 + 1] * sz : py * sz};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    // Flip each triangle: XY→XZ remap flips handedness, reversing winding.
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles; ++i) {
        indices[i * 3 + 0] = static_cast<uint32_t>(m->triangles[i * 3 + 0]);
        indices[i * 3 + 1] = static_cast<uint32_t>(m->triangles[i * 3 + 2]);
        indices[i * 3 + 2] = static_cast<uint32_t>(m->triangles[i * 3 + 1]);
    }
    par_shapes_free_mesh(m);

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateSphere(const Engine::SphereParams& p)
{
    ZoneScopedN("GenerateSphere");

    const int slices = std::max(3, p.slices);
    const int stacks = std::max(3, p.stacks);
    const float r = p.radius;
    const auto pi = glm::pi<float>();

    // par_shapes sphere: Z-up. Remap to Y-up: engine = (par.x*r, par.z*r, -par.y*r)
    par_shapes_mesh* m = par_shapes_create_parametric_sphere(slices, stacks);
    if (!m) return false;

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const Vec3 pos = {px * r, pz * r, -py * r};
        vertices[i].position = pos;
        const float nx = m->normals[i * 3 + 0], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        vertices[i].normal = glm::normalize(Vec3{nx, nz, -ny});
        // Spherical UV: u = longitude 0→1, v = latitude 0(south)→1(north)
        vertices[i].uv = {0.5f - atan2f(-pos.z, pos.x) / (2.0f * pi), pos.y / r * 0.5f + 0.5f};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) {
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
    }
    par_shapes_free_mesh(m);

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateSubdividedSphere(const Engine::SubdividedSphereParams& p)
{
    ZoneScopedN("GenerateSubdividedSphere");

    const int subd = glm::clamp(p.subdivisions, 0, 4);
    const float r = p.radius;
    const auto pi = glm::pi<float>();

    par_shapes_mesh* m = par_shapes_create_subdivided_sphere(subd);
    if (!m) return false;

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const Vec3 pos = glm::normalize(Vec3{
                                  m->points[i * 3 + 0], m->points[i * 3 + 1], m->points[i * 3 + 2]
                              }) * r;
        vertices[i].position = pos;
        vertices[i].normal = glm::normalize(pos);
        vertices[i].uv = {0.5f - atan2f(pos.z, pos.x) / (2.0f * pi), asinf(glm::clamp(pos.y / r, -1.0f, 1.0f)) / pi + 0.5f};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) {
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
    }
    par_shapes_free_mesh(m);

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateHemisphere(const Engine::HemisphereParams& p)
{
    ZoneScopedN("GenerateHemisphere");

    const int N = std::max(3, p.slices);
    const int HR = std::max(2, p.stacks);
    const float r = p.radius;
    const float pi = glm::pi<float>();

    // verts: 1 pole + HR*(N+1) rings + 1 cap center + N cap ring; indices: 6*N*HR
    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 2 + HR * (N + 1) + N);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 6 * N * HR);
    {
        Engine::FullVertex v{};
        v.position = {0, r, 0};
        v.normal = {0, 1, 0};
        v.uv = {0.5f, 1.0f};
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.PushBack(v);
    }
    const uint32_t pole = 0;

    Core::HeapArray<uint32_t> ringBase(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, HR + 1);
    for (int i = 1; i <= HR; i++) {
        const float phi = pi * 0.5f * (1.0f - static_cast<float>(i) / HR);
        ringBase[i] = static_cast<uint32_t>(vertices.Size());
        const float ringR = r * cosf(phi);
        const float ringY = r * sinf(phi);
        const float nY = sinf(phi), nXZ = cosf(phi);
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / N * 2.0f * pi;
            Engine::FullVertex v{};
            v.position = {cosf(angle) * ringR, ringY, sinf(angle) * ringR};
            v.normal = {cosf(angle) * nXZ, nY, sinf(angle) * nXZ};
            v.uv = {static_cast<float>(j) / N, static_cast<float>(HR - i) / HR};
            v.tangent = {-sinf(angle), 0, cosf(angle), 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        }
    }

    for (int j = 0; j < N; j++) {
        const uint32_t tri[] = {pole, ringBase[1] + (uint32_t)j + 1, ringBase[1] + (uint32_t)j};
        indices.Append(tri, tri + 3);
    }

    for (int i = 1; i < HR; i++) {
        for (int j = 0; j < N; j++) {
            uint32_t a0 = ringBase[i] + j, a1 = ringBase[i] + j + 1;
            uint32_t b0 = ringBase[i + 1] + j, b1 = ringBase[i + 1] + j + 1;
            const uint32_t quad[] = {a0, a1, b0, b0, a1, b1};
            indices.Append(quad, quad + 6);
        }
    }

    uint32_t capBase = static_cast<uint32_t>(vertices.Size()); {
        Engine::FullVertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.uv = {0.5f, 0.5f};
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.PushBack(cv);
    }
    for (int j = 0; j < N; j++) {
        const float angle = static_cast<float>(j) / N * 2.0f * pi;
        Engine::FullVertex v{};
        v.position = {cosf(angle) * r, 0.0f, sinf(angle) * r};
        v.normal = {0, -1, 0};
        v.uv = {cosf(angle) * 0.5f + 0.5f, sinf(angle) * 0.5f + 0.5f};
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.PushBack(v);
    }
    for (int j = 0; j < N; j++) {
        const uint32_t tri[] = {capBase, capBase + 1 + (uint32_t)j, capBase + 1 + (uint32_t)((j + 1) % N)};
        indices.Append(tri, tri + 3);
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GeneratePipe(const Engine::PipeParams& p)
{
    ZoneScopedN("GeneratePipe");

    const int N = std::max(3, p.slices);
    const float ro = std::max(0.01f, p.outerRadius);
    const float ri = std::max(0.001f, std::min(p.innerRadius, ro - 0.001f));
    const float h = p.height;
    const float hh = h * 0.5f;
    const auto pi = glm::pi<float>();

    // 4 ring sections × (N+1)*2 verts = 8*(N+1); 4 sections × N*6 = 24*N indices
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 8 * (N + 1));
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, 24 * N);
    size_t vi = 0, ii = 0;

    auto addRingQuads = [&](float radius, bool outward) {
        // Outer or inner side cylinder wall
        auto base = static_cast<uint32_t>(vi);
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * pi;
            const float cx = cosf(angle), cz = sinf(angle);
            Vec3 n = outward ? Vec3{cx, 0, cz} : Vec3{-cx, 0, -cz};
            for (int k = 0; k < 2; k++) {
                Engine::FullVertex v{};
                v.position = {cx * radius, k == 0 ? -hh : hh, cz * radius};
                v.normal = n;
                v.uv = {static_cast<float>(j) / static_cast<float>(N), k == 0 ? 0.0f : 1.0f};
                v.tangent = {-cz, 0, cx, 1.0f};
                v.color = {1, 1, 1, 1};
                vertices[vi++] = v;
            }
        }
        for (int j = 0; j < N; j++) {
            uint32_t a0 = base + j * 2, a1 = base + j * 2 + 1;
            uint32_t b0 = base + (j + 1) * 2, b1 = base + (j + 1) * 2 + 1;
            if (outward) {
                // Outer: CCW from outside
                indices[ii++] = a0; indices[ii++] = a1; indices[ii++] = b0;
                indices[ii++] = a1; indices[ii++] = b1; indices[ii++] = b0;
            }
            else {
                // Inner: CCW from inside
                indices[ii++] = a0; indices[ii++] = b0; indices[ii++] = a1;
                indices[ii++] = a1; indices[ii++] = b0; indices[ii++] = b1;
            }
        }
    };

    addRingQuads(ro, true); // outer wall
    addRingQuads(ri, false); // inner wall

    // Top annular cap (+Y): fan from outer ring to inner ring
    {
        auto base = static_cast<uint32_t>(vi);
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * pi;
            const float cx = cosf(angle), cz = sinf(angle);
            // Outer edge vertex
            Engine::FullVertex vo{};
            vo.position = {cx * ro, hh, cz * ro};
            vo.normal = {0, 1, 0};
            vo.uv = {-cx * ro, cz * ro};
            vo.tangent = {-1, 0, 0, 1};
            vo.color = {1, 1, 1, 1};
            vertices[vi++] = vo;
            // Inner edge vertex
            Engine::FullVertex vinner{};
            vinner.position = {cx * ri, hh, cz * ri};
            vinner.normal = {0, 1, 0};
            vinner.uv = {-cx * ri, cz * ri};
            vinner.tangent = {-1, 0, 0, 1};
            vinner.color = {1, 1, 1, 1};
            vertices[vi++] = vinner;
        }
        for (int j = 0; j < N; j++) {
            uint32_t oa = base + j * 2, oi = base + j * 2 + 1;
            uint32_t na = base + (j + 1) * 2, ni = base + (j + 1) * 2 + 1;
            indices[ii++] = oa; indices[ii++] = oi; indices[ii++] = na;
            indices[ii++] = oi; indices[ii++] = ni; indices[ii++] = na;
        }
    }

    // Bottom annular cap (-Y): reversed winding
    {
        auto base = static_cast<uint32_t>(vi);
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * pi;
            const float cx = cosf(angle), cz = sinf(angle);
            Engine::FullVertex vo{};
            vo.position = {cx * ro, -hh, cz * ro};
            vo.normal = {0, -1, 0};
            vo.uv = {cx * ro, cz * ro};
            vo.tangent = {1, 0, 0, 1};
            vo.color = {1, 1, 1, 1};
            vertices[vi++] = vo;
            Engine::FullVertex vinner{};
            vinner.position = {cx * ri, -hh, cz * ri};
            vinner.normal = {0, -1, 0};
            vinner.uv = {cx * ri, cz * ri};
            vinner.tangent = {1, 0, 0, 1};
            vinner.color = {1, 1, 1, 1};
            vertices[vi++] = vinner;
        }
        for (int j = 0; j < N; j++) {
            uint32_t oa = base + j * 2, oi = base + j * 2 + 1;
            uint32_t na = base + (j + 1) * 2, ni = base + (j + 1) * 2 + 1;
            indices[ii++] = oa; indices[ii++] = na; indices[ii++] = oi;
            indices[ii++] = oi; indices[ii++] = na; indices[ii++] = ni;
        }
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateTetrahedron(const Engine::TetrahedronParams& p)
{
    ZoneScopedN("GenerateTetrahedron");

    par_shapes_mesh* m = par_shapes_create_tetrahedron();
    if (!m) return false;

    par_shapes_unweld(m, true);
    par_shapes_compute_normals(m);

    const float r = p.radius;
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->npoints));
    for (int i = 0; i < m->npoints; ++i) {
        // Tetrahedron is already Y-up
        const Vec3 pos = Vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * r;
        const Vec3 n = glm::normalize(Vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]});
        const Vec3 absN = glm::abs(n);
        float u, v;
        if (absN.y >= absN.x && absN.y >= absN.z) {
            u = (n.y > 0.0f) ? -pos.x : pos.x;
            v = pos.z;
        }
        else if (absN.x >= absN.z) {
            u = (n.x > 0.0f) ? -pos.z : pos.z;
            v = pos.y;
        }
        else {
            u = (n.z < 0.0f) ? -pos.x : pos.x;
            v = pos.y;
        }
        vertices[i].position = pos;
        vertices[i].normal = n;
        vertices[i].uv = {u, v};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->ntriangles) * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) { indices[i] = static_cast<uint32_t>(m->triangles[i]); }
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateOctahedron(const Engine::OctahedronParams& p)
{
    ZoneScopedN("GenerateOctahedron");

    par_shapes_mesh* m = par_shapes_create_octahedron();
    if (!m) return false;

    par_shapes_unweld(m, true);
    par_shapes_compute_normals(m);

    const float r = p.radius;
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->npoints));
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const Vec3 pos = Vec3{px, pz, -py} * r; // Z-up to Y-up
        const float nx = m->normals[i * 3], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        const Vec3 n = glm::normalize(Vec3{nx, nz, -ny});
        const Vec3 absN = glm::abs(n);
        float u, v;
        if (absN.y >= absN.x && absN.y >= absN.z) {
            u = (n.y > 0.0f) ? -pos.x : pos.x;
            v = pos.z;
        }
        else if (absN.x >= absN.z) {
            u = (n.x > 0.0f) ? -pos.z : pos.z;
            v = pos.y;
        }
        else {
            u = (n.z < 0.0f) ? -pos.x : pos.x;
            v = pos.y;
        }
        vertices[i].position = pos;
        vertices[i].normal = n;
        vertices[i].uv = {u, v};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->ntriangles) * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) { indices[i] = static_cast<uint32_t>(m->triangles[i]); }
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateIcosahedron(const Engine::IcosahedronParams& p)
{
    ZoneScopedN("GenerateIcosahedron");

    par_shapes_mesh* m = par_shapes_create_icosahedron();
    if (!m) return false;

    par_shapes_unweld(m, true);
    par_shapes_compute_normals(m);

    const float r = p.radius;
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->npoints));
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const Vec3 pos = Vec3{px, pz, -py} * r; // Z-up to Y-up
        const float nx = m->normals[i * 3], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        const Vec3 n = glm::normalize(Vec3{nx, nz, -ny});
        const Vec3 absN = glm::abs(n);
        float u, v;
        if (absN.y >= absN.x && absN.y >= absN.z) {
            u = (n.y > 0.0f) ? -pos.x : pos.x;
            v = pos.z;
        }
        else if (absN.x >= absN.z) {
            u = (n.x > 0.0f) ? -pos.z : pos.z;
            v = pos.y;
        }
        else {
            u = (n.z < 0.0f) ? -pos.x : pos.x;
            v = pos.y;
        }
        vertices[i].position = pos;
        vertices[i].normal = n;
        vertices[i].uv = {u, v};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->ntriangles) * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) { indices[i] = static_cast<uint32_t>(m->triangles[i]); }
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateDodecahedron(const Engine::DodecahedronParams& p)
{
    ZoneScopedN("GenerateDodecahedron");

    par_shapes_mesh* m = par_shapes_create_dodecahedron();
    if (!m) return false;

    par_shapes_unweld(m, true);
    par_shapes_compute_normals(m);

    const float r = p.radius;
    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->npoints));
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const Vec3 pos = Vec3{px, pz, -py} * r; // Z-up to Y-up
        const float nx = m->normals[i * 3], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        const Vec3 n = glm::normalize(Vec3{nx, nz, -ny});
        const Vec3 absN = glm::abs(n);
        float u, v;
        if (absN.y >= absN.x && absN.y >= absN.z) {
            u = (n.y > 0.0f) ? -pos.x : pos.x;
            v = pos.z;
        }
        else if (absN.x >= absN.z) {
            u = (n.x > 0.0f) ? -pos.z : pos.z;
            v = pos.y;
        }
        else {
            u = (n.z < 0.0f) ? -pos.x : pos.x;
            v = pos.y;
        }
        vertices[i].position = pos;
        vertices[i].normal = n;
        vertices[i].uv = {u, v};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->ntriangles) * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) { indices[i] = static_cast<uint32_t>(m->triangles[i]); }
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateKleinBottle(const Engine::KleinBottleParams& p)
{
    ZoneScopedN("GenerateKleinBottle");

    par_shapes_mesh* m = par_shapes_create_klein_bottle(std::max(3, p.slices), std::max(3, p.stacks));
    if (!m) return false;

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->npoints));
    for (int i = 0; i < m->npoints; ++i) {
        vertices[i].position = Vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * p.scale;
        vertices[i].normal = m->normals ? glm::normalize(Vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]}) : Vec3{0, 1, 0};
        vertices[i].uv = {m->tcoords ? m->tcoords[i * 2] : 0.0f, m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->ntriangles) * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) { indices[i] = static_cast<uint32_t>(m->triangles[i]); }
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateTrefoilKnot(const Engine::TrefoilKnotParams& p)
{
    ZoneScopedN("GenerateTrefoilKnot");

    const float tubeRadius = glm::clamp(p.tubeRadius, 0.5f, 3.0f);
    par_shapes_mesh* m = par_shapes_create_trefoil_knot(std::max(3, p.slices), std::max(3, p.stacks), tubeRadius);
    if (!m) return false;

    Core::HeapArray<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->npoints));
    for (int i = 0; i < m->npoints; ++i) {
        vertices[i].position = Vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * p.scale;
        vertices[i].normal = m->normals ? glm::normalize(Vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]}) : Vec3{0, 1, 0};
        vertices[i].uv = {m->tcoords ? m->tcoords[i * 2] : 0.0f, m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    Core::HeapArray<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(m->ntriangles) * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) { indices[i] = static_cast<uint32_t>(m->triangles[i]); }
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateCurvedRamp(const Engine::CurvedRampParams& p)
{
    ZoneScopedN("GenerateCurvedRamp");

    const float w = std::max(0.01f, p.width);
    const float h = std::max(0.01f, p.height);
    const float R = glm::clamp(p.radius, 0.01f, h);
    const int seg = std::max(2, p.segments);
    const float pi = glm::pi<float>();
    const float hw = w * 0.5f;
    const float fl = p.bHalfPipe ? std::max(0.0f, p.flatLength) : 0.0f;
    const float lip = std::max(0.0f, p.lipHeight);

    // Profile in YZ plane. Normals point toward the concave (rider) side.
    // Curve 1 center: (Z=R, Y=R). Angle PI (top) to 3PI/2 (bottom).
    // Curve 2 center: (Z=R+fl, Y=R). Angle -PI/2 (bottom) to 0 (top).
    const int profCapacity = (h > R ? 2 : 0) + (seg + 1)
        + (p.bHalfPipe ? (fl > 0.0f ? 1 : 0) + (seg + 1) + (h > R ? 2 : 0) : 0);
    Core::HeapArray<Vec3> profile(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    Core::HeapArray<Vec3> profileN(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    int pi_ = 0;

    if (h > R) {
        profile[pi_] = {0.0f, lip + h, 0.0f};
        profileN[pi_] = {0.0f, 0.0f, 1.0f};
        pi_++;
        profile[pi_] = {0.0f, lip + R, 0.0f};
        profileN[pi_] = {0.0f, 0.0f, 1.0f};
        pi_++;
    }

    for (int i = 0; i <= seg; i++) {
        const float t = static_cast<float>(i) / seg;
        const float angle = pi + t * (pi * 0.5f);
        const float z = R + R * cosf(angle);
        const float y = lip + R + R * sinf(angle);
        profile[pi_] = {0.0f, y, z};
        profileN[pi_] = glm::normalize(Vec3{0.0f, -sinf(angle), -cosf(angle)});
        pi_++;
    }

    if (p.bHalfPipe) {
        if (fl > 0.0f) {
            profile[pi_] = {0.0f, lip, R + fl};
            profileN[pi_] = {0.0f, 1.0f, 0.0f};
            pi_++;
        }

        const float zCenter2 = R + fl;
        for (int i = 0; i <= seg; i++) {
            const float t = static_cast<float>(i) / seg;
            const float angle = -pi * 0.5f + t * (pi * 0.5f);
            const float z = zCenter2 + R * cosf(angle);
            const float y = lip + R + R * sinf(angle);
            profile[pi_] = {0.0f, y, z};
            profileN[pi_] = glm::normalize(Vec3{0.0f, -sinf(angle), -cosf(angle)});
            pi_++;
        }

        if (h > R) {
            const float zBack = 2.0f * R + fl;
            profile[pi_] = {0.0f, lip + R, zBack};
            profileN[pi_] = {0.0f, 0.0f, -1.0f};
            pi_++;
            profile[pi_] = {0.0f, lip + h, zBack};
            profileN[pi_] = {0.0f, 0.0f, -1.0f};
            pi_++;
        }
    }

    const int profCount = profCapacity;
    const float totalDepth = p.bHalfPipe ? 2.0f * R + fl : R;

    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);

    auto pushVert = [&](Vec3 pos, Vec3 n, float u, float v) {
        Engine::FullVertex vtx{};
        vtx.position = pos;
        vtx.normal = n;
        vtx.uv = {u, v};
        vtx.tangent = {1, 0, 0, 1};
        vtx.color = {1, 1, 1, 1};
        vertices.PushBack(vtx);
    };

    // Riding surface: extrude profile along X
    uint32_t surfBase = static_cast<uint32_t>(vertices.Size());
    for (int side = 0; side < 2; side++) {
        const float x = side == 0 ? -hw : hw;
        const float u = side == 0 ? 0.0f : 1.0f;
        for (int i = 0; i < profCount; i++) {
            const float v = static_cast<float>(i) / (profCount - 1);
            pushVert({x, profile[i].y, profile[i].z}, profileN[i], u, v);
        }
    }
    for (int i = 0; i < profCount - 1; i++) {
        uint32_t a0 = surfBase + i;
        uint32_t a1 = surfBase + i + 1;
        uint32_t b0 = surfBase + profCount + i;
        uint32_t b1 = surfBase + profCount + i + 1;
        const uint32_t tri[6] = {a0, a1, b0, b0, a1, b1};
        indices.Append(tri, tri + 6);
    }

    // Back wall at Z=0
    {
        uint32_t base = static_cast<uint32_t>(vertices.Size());
        Vec3 n = {0, 0, -1};
        pushVert({-hw, 0, 0}, n, 0, 0);
        pushVert({hw, 0, 0}, n, 1, 0);
        pushVert({hw, lip + h, 0}, n, 1, 1);
        pushVert({-hw, lip + h, 0}, n, 0, 1);
        const uint32_t tri[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
        indices.Append(tri, tri + 6);
    }

    // Far wall (half-pipe only)
    if (p.bHalfPipe) {
        uint32_t base = static_cast<uint32_t>(vertices.Size());
        Vec3 n = {0, 0, 1};
        pushVert({-hw, 0, totalDepth}, n, 0, 0);
        pushVert({hw, 0, totalDepth}, n, 1, 0);
        pushVert({hw, lip + h, totalDepth}, n, 1, 1);
        pushVert({-hw, lip + h, totalDepth}, n, 0, 1);
        const uint32_t tri[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        indices.Append(tri, tri + 6);
    }

    // Bottom
    {
        uint32_t base = static_cast<uint32_t>(vertices.Size());
        Vec3 n = {0, -1, 0};
        pushVert({-hw, 0, 0}, n, 0, 0);
        pushVert({hw, 0, 0}, n, 1, 0);
        pushVert({hw, 0, totalDepth}, n, 1, 1);
        pushVert({-hw, 0, totalDepth}, n, 0, 1);
        const uint32_t tri[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        indices.Append(tri, tri + 6);
    }

    // Side walls
    auto emitFan = [&](uint32_t base, int count, int side) {
        for (int i = 1; i < count - 1; i++) {
            if (side == 0) {
                const uint32_t tri[3] = {base, base + (uint32_t)(i + 1), base + (uint32_t)i};
                indices.Append(tri, tri + 3);
            }
            else {
                const uint32_t tri[3] = {base, base + (uint32_t)i, base + (uint32_t)(i + 1)};
                indices.Append(tri, tri + 3);
            }
        }
    };

    auto sideUV = [&](int i) -> Vec2 {
        return {profile[i].z / std::max(totalDepth, 0.01f), profile[i].y / std::max(h, 0.01f)};
    };

    // Profile split index: end of curve 1 + flat section (where Y=0 before curve 2)
    const int splitIdx = (h > R ? 2 : 0) + (seg + 1) + (fl > 0.0f ? 1 : 0);

    for (int side = 0; side < 2; side++) {
        const float x = side == 0 ? -hw : hw;
        const Vec3 n = side == 0 ? Vec3{-1, 0, 0} : Vec3{1, 0, 0};

        if (!p.bHalfPipe) {
            uint32_t base = static_cast<uint32_t>(vertices.Size());
            pushVert({x, 0, 0}, n, 0, 0);
            pushVert({x, lip + h, 0}, n, 0, 1);
            for (int i = 0; i < profCount; i++) {
                auto uv = sideUV(i);
                pushVert({x, profile[i].y, profile[i].z}, n, uv.x, uv.y);
            }
            pushVert({x, 0, totalDepth}, n, 1, 0);
            emitFan(base, 2 + profCount + 1, side);
        }
        else {
            // Near fan: bottom-near corner, wall 1, curve 1, flat
            {
                uint32_t base = static_cast<uint32_t>(vertices.Size());
                pushVert({x, 0, 0}, n, 0, 0);
                pushVert({x, lip + h, 0}, n, 0, 1);
                for (int i = 0; i < splitIdx; i++) {
                    auto uv = sideUV(i);
                    pushVert({x, profile[i].y, profile[i].z}, n, uv.x, uv.y);
                }
                emitFan(base, 2 + splitIdx, side);
            }
            // Far fan: bottom-far corner, curve 2, wall 2
            {
                uint32_t base = static_cast<uint32_t>(vertices.Size());
                pushVert({x, 0, totalDepth}, n, 1, 0);
                for (int i = splitIdx; i < profCount; i++) {
                    auto uv = sideUV(i);
                    pushVert({x, profile[i].y, profile[i].z}, n, uv.x, uv.y);
                }
                pushVert({x, lip + h, totalDepth}, n, 1, 1);
                emitFan(base, 1 + (profCount - splitIdx) + 1, side);
            }
        }
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateBowl(const Engine::BowlParams& p)
{
    ZoneScopedN("GenerateBowl");

    const float outerR = std::max(0.01f, p.radius);
    const float h = std::max(0.01f, p.height);
    const float cR = glm::clamp(p.curveRadius, 0.01f, h);
    const float flatR = glm::clamp(p.flatRadius, 0.0f, outerR - cR);
    const float lip = std::max(0.0f, p.lipHeight);
    const int N = std::max(3, p.slices);
    const int seg = std::max(2, p.segments);
    const float pi = glm::pi<float>();

    // Profile in YR plane (R = radial distance from center, Y = height).
    // Revolved around Y axis. Normals point inward (toward bowl center).
    // Curve center: (R = flatR, Y = cR). Angle 0 (wall) to -PI/2 (floor).
    const int profCapacity = (h > cR ? 2 : 0) + (seg + 1) + (flatR > 0.0f ? 1 : 0);
    Core::HeapArray<float> profR(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    Core::HeapArray<float> profY(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    Core::HeapArray<Vec2> profN(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    int pci = 0;

    // Vertical wall above the curve (if h > cR)
    if (h > cR) {
        profR[pci] = flatR + cR; profY[pci] = lip + h; profN[pci] = {-1.0f, 0.0f}; pci++;
        profR[pci] = flatR + cR; profY[pci] = lip + cR; profN[pci] = {-1.0f, 0.0f}; pci++;
    }

    // Quarter-circle from wall down to floor
    for (int i = 0; i <= seg; i++) {
        const float t = static_cast<float>(i) / seg;
        const float angle = -t * (pi * 0.5f); // 0 to -PI/2
        profR[pci] = flatR + cR * cosf(angle);
        profY[pci] = lip + cR + cR * sinf(angle);
        profN[pci] = {-cosf(angle), -sinf(angle)};
        pci++;
    }

    // Flat floor (if flatR > 0)
    if (flatR > 0.0f) {
        profR[pci] = 0.0f; profY[pci] = lip; profN[pci] = {0.0f, 1.0f}; pci++;
    }

    const int profCount = profCapacity;

    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);

    // Revolve profile around Y axis
    for (int j = 0; j <= N; j++) {
        const float theta = static_cast<float>(j) / N * 2.0f * pi;
        const float cx = cosf(theta), cz = sinf(theta);
        for (int i = 0; i < profCount; i++) {
            Engine::FullVertex v{};
            v.position = {cx * profR[i], profY[i], cz * profR[i]};
            v.normal = {cx * profN[i].x, profN[i].y, cz * profN[i].x};
            v.uv = {static_cast<float>(j) / N, static_cast<float>(i) / (profCount - 1)};
            v.tangent = {-cz, 0, cx, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        }
    }

    for (int j = 0; j < N; j++) {
        for (int i = 0; i < profCount - 1; i++) {
            uint32_t a0 = j * profCount + i;
            uint32_t a1 = j * profCount + i + 1;
            uint32_t b0 = (j + 1) * profCount + i;
            uint32_t b1 = (j + 1) * profCount + i + 1;
            const uint32_t tri[6] = {a0, a1, b0, b0, a1, b1};
            indices.Append(tri, tri + 6);
        }
    }

    // Bottom face at Y=0 (pivot point)
    {
        uint32_t base = static_cast<uint32_t>(vertices.Size());
        Engine::FullVertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.uv = {0.5f, 0.5f};
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.PushBack(cv);

        const float rimR = flatR + cR;
        for (int j = 0; j < N; j++) {
            const float theta = static_cast<float>(j) / N * 2.0f * pi;
            Engine::FullVertex v{};
            v.position = {cosf(theta) * rimR, 0.0f, sinf(theta) * rimR};
            v.normal = {0, -1, 0};
            v.uv = {cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f};
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.PushBack(v);
        }
        for (int j = 0; j < N; j++) {
            const uint32_t tri[3] = {base, base + 1 + (uint32_t)j, base + 1 + (uint32_t)((j + 1) % N)};
            indices.Append(tri, tri + 3);
        }
    }

    // Outer wall (vertical cylinder at R = flatR + cR, from Y=0 to Y=lip+h)
    {
        const float wallR = flatR + cR;
        uint32_t base = static_cast<uint32_t>(vertices.Size());
        for (int j = 0; j <= N; j++) {
            const float theta = static_cast<float>(j) / N * 2.0f * pi;
            const float cx = cosf(theta), cz = sinf(theta);
            for (int k = 0; k < 2; k++) {
                Engine::FullVertex v{};
                v.position = {cx * wallR, k == 0 ? 0.0f : lip + h, cz * wallR};
                v.normal = {cx, 0, cz};
                v.uv = {static_cast<float>(j) / N, static_cast<float>(k)};
                v.tangent = {-cz, 0, cx, 1.0f};
                v.color = {1, 1, 1, 1};
                vertices.PushBack(v);
            }
        }
        for (int j = 0; j < N; j++) {
            uint32_t a0 = base + j * 2, a1 = base + j * 2 + 1;
            uint32_t b0 = base + (j + 1) * 2, b1 = base + (j + 1) * 2 + 1;
            const uint32_t tri[6] = {a0, a1, b0, a1, b1, b0};
            indices.Append(tri, tri + 6);
        }
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateSpiralStaircase(const Engine::SpiralStaircaseParams& p)
{
    ZoneScopedN("GenerateSpiralStaircase");

    if (p.stepCount <= 0) { return false; }

    const int32_t steps = p.stepCount;
    const int32_t seg = std::max(1, p.arcSegments);
    const float ro = std::max(0.05f, p.outerRadius);
    const float ri = glm::clamp(p.centerColumnRadius, 0.001f, ro - 0.01f);
    const float stepH = std::max(0.001f, p.bSpecifyStepHeight ? p.stepHeight : p.totalHeight / static_cast<float>(steps));
    const float thick = glm::clamp(p.treadThickness, 0.001f, stepH);
    const float dStep = glm::radians(p.bSpecifyDegreesPerStep ? p.degreesPerStep : p.totalSweep / static_cast<float>(steps));

    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    vertices.Reserve(static_cast<size_t>(steps) * (seg + 1) * 24 + 256);
    indices.Reserve(static_cast<size_t>(steps) * seg * 36 + 512);

    auto pushV = [&](Vec3 pos, Vec3 n, Vec2 uv, Vec4 t) -> uint32_t {
        Engine::FullVertex v{};
        v.position = pos;
        v.normal = n;
        v.uv = uv;
        v.tangent = t;
        v.color = {1.0f, 1.0f, 1.0f, 1.0f};
        const auto idx = static_cast<uint32_t>(vertices.Size());
        vertices.PushBack(v);
        return idx;
    };

    // Winding is chosen so the emitted triangle's geometric normal aligns with the supplied normal.
    auto addTri = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 n, Vec4 t, Vec2 u0, Vec2 u1, Vec2 u2) {
        const bool flip = glm::dot(glm::cross(p1 - p0, p2 - p0), n) < 0.0f;
        const uint32_t i0 = pushV(p0, n, u0, t);
        const uint32_t i1 = pushV(p1, n, u1, t);
        const uint32_t i2 = pushV(p2, n, u2, t);
        if (!flip) {
            indices.PushBack(i0); indices.PushBack(i1); indices.PushBack(i2);
        }
        else {
            indices.PushBack(i0); indices.PushBack(i2); indices.PushBack(i1);
        }
    };
    auto addQuad = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 n, Vec4 t, Vec2 u0, Vec2 u1, Vec2 u2, Vec2 u3) {
        addTri(p0, p1, p2, n, t, u0, u1, u2);
        addTri(p0, p2, p3, n, t, u0, u2, u3);
    };

    auto ringDir = [](float a) { return Vec3{cosf(a), 0.0f, sinf(a)}; };

    // Stair mode emits flat treads (constant height per step); ramp mode replaces them with a continuous helicoid slab whose top tracks angle.
    const bool bRamp = p.bRamp;
    const float heightPerRad = stepH / std::max(dStep, 1e-6f);
    const float rMid = 0.5f * (ri + ro);
    auto topAtAngle = [&](float a) { return a * heightPerRad; };

    for (int32_t i = 0; i < steps; ++i) {
        const float a0 = static_cast<float>(i) * dStep;
        const float a1 = a0 + dStep;
        const float ytStep = static_cast<float>(i + 1) * stepH;

        for (int32_t s = 0; s < seg; ++s) {
            const float as = a0 + (a1 - a0) * (static_cast<float>(s) / static_cast<float>(seg));
            const float an = a0 + (a1 - a0) * (static_cast<float>(s + 1) / static_cast<float>(seg));
            const Vec3 ds = ringDir(as), dn = ringDir(an);

            const float yts = bRamp ? topAtAngle(as) : ytStep;
            const float ytn = bRamp ? topAtAngle(an) : ytStep;
            const Vec3 topS{0.0f, yts, 0.0f}, topN{0.0f, ytn, 0.0f};
            const Vec3 botS{0.0f, yts - thick, 0.0f}, botN{0.0f, ytn - thick, 0.0f};

            const float amid = 0.5f * (as + an);
            const Vec3 topNormal = bRamp ? glm::normalize(Vec3{heightPerRad * sinf(amid), rMid, -heightPerRad * cosf(amid)}) : Vec3{0, 1, 0};

            addQuad(ds * ri + topS, ds * ro + topS, dn * ro + topN, dn * ri + topN,
                    topNormal, Vec4{ds.z, 0, -ds.x, 1},
                    Vec2{ri, as}, Vec2{ro, as}, Vec2{ro, an}, Vec2{ri, an});
            addQuad(ds * ri + botS, ds * ro + botS, dn * ro + botN, dn * ri + botN,
                    -topNormal, Vec4{ds.z, 0, -ds.x, 1},
                    Vec2{ri, as}, Vec2{ro, as}, Vec2{ro, an}, Vec2{ri, an});
            addQuad(ds * ro + botS, dn * ro + botN, dn * ro + topN, ds * ro + topS,
                    glm::normalize(ds + dn), Vec4{-ds.z, 0, ds.x, 1},
                    Vec2{as, yts - thick}, Vec2{an, ytn - thick}, Vec2{an, ytn}, Vec2{as, yts});
            addQuad(ds * ri + botS, dn * ri + botN, dn * ri + topN, ds * ri + topS,
                    -glm::normalize(ds + dn), Vec4{-ds.z, 0, ds.x, 1},
                    Vec2{as, yts - thick}, Vec2{an, ytn - thick}, Vec2{an, ytn}, Vec2{as, yts});
        }

        // Radial faces are the per-step risers in stair mode; in ramp mode only the bottom lip (first step) and top lip (last step) remain.
        const Vec3 d0 = ringDir(a0), d1 = ringDir(a1);
        const float yt0 = bRamp ? topAtAngle(a0) : ytStep;
        const float yt1 = bRamp ? topAtAngle(a1) : ytStep;
        if (!bRamp || i == 0) {
            addQuad(d0 * ri + Vec3{0, yt0 - thick, 0}, d0 * ro + Vec3{0, yt0 - thick, 0}, d0 * ro + Vec3{0, yt0, 0}, d0 * ri + Vec3{0, yt0, 0},
                    Vec3{sinf(a0), 0, -cosf(a0)}, Vec4{d0.x, 0, d0.z, 1},
                    Vec2{ri, yt0 - thick}, Vec2{ro, yt0 - thick}, Vec2{ro, yt0}, Vec2{ri, yt0});
        }
        if (!bRamp || i == steps - 1) {
            addQuad(d1 * ri + Vec3{0, yt1 - thick, 0}, d1 * ro + Vec3{0, yt1 - thick, 0}, d1 * ro + Vec3{0, yt1, 0}, d1 * ri + Vec3{0, yt1, 0},
                    Vec3{-sinf(a1), 0, cosf(a1)}, Vec4{d1.x, 0, d1.z, 1},
                    Vec2{ri, yt1 - thick}, Vec2{ro, yt1 - thick}, Vec2{ro, yt1}, Vec2{ri, yt1});
        }
    }

    if (p.bShowCenterColumn) {
        const float pi = glm::pi<float>();
        const int32_t colSlices = std::max(16, seg * 4);
        const float colTop = static_cast<float>(steps) * stepH;
        const Vec3 capTop{0.0f, colTop, 0.0f}, capBot{0.0f, 0.0f, 0.0f};

        for (int32_t s = 0; s < colSlices; ++s) {
            const float as = static_cast<float>(s) / static_cast<float>(colSlices) * 2.0f * pi;
            const float an = static_cast<float>(s + 1) / static_cast<float>(colSlices) * 2.0f * pi;
            const Vec3 ds = ringDir(as), dn = ringDir(an);

            addQuad(ds * ri + capBot, dn * ri + capBot, dn * ri + capTop, ds * ri + capTop,
                    glm::normalize(ds + dn), Vec4{-ds.z, 0, ds.x, 1},
                    Vec2{as, 0}, Vec2{an, 0}, Vec2{an, colTop}, Vec2{as, colTop});
            addTri(capTop, ds * ri + capTop, dn * ri + capTop, Vec3{0, 1, 0}, Vec4{1, 0, 0, 1},
                   Vec2{0, 0}, Vec2{ds.x * ri, ds.z * ri}, Vec2{dn.x * ri, dn.z * ri});
            addTri(capBot, ds * ri + capBot, dn * ri + capBot, Vec3{0, -1, 0}, Vec4{1, 0, 0, 1},
                   Vec2{0, 0}, Vec2{ds.x * ri, ds.z * ri}, Vec2{dn.x * ri, dn.z * ri});
        }
    }

    return FinalizeGeometry(Core::Span<const Engine::FullVertex>(vertices.Data(), vertices.Size()), Core::Span<const uint32_t>(indices.Data(), indices.Size()));
}

struct SplineProfilePoint
{
    Vec2 pos;
    Vec2 normal;
};

static float ProfileCross2(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool ProfilePointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c)
{
    const float d1 = ProfileCross2(p, a, b);
    const float d2 = ProfileCross2(p, b, c);
    const float d3 = ProfileCross2(p, c, a);
    const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

// Consecutive (and wrap) duplicate-free CCW outline of the swept ring, used for cap triangulation.
static void ProfileOutline(const Core::Vector<SplineProfilePoint>& ring, Core::Vector<Vec2>& outline)
{
    const int n = static_cast<int>(ring.Size());
    for (int i = 0; i < n; i++) {
        const Vec2 cur = ring[i].pos;
        if (!outline.IsEmpty() && glm::distance(cur, outline.Back()) < 1e-6f) { continue; }
        outline.PushBack(cur);
    }
    while (outline.Size() >= 2 && glm::distance(outline.Back(), outline[0]) < 1e-6f) {
        outline.RemoveAt(outline.Size() - 1);
    }
}

// Ear-clip a simple CCW polygon into triangle indices (handles concave profiles like I-beam/U-channel).
static void TriangulateProfileCap(const Core::Vector<Vec2>& poly, Core::Vector<uint32_t>& outTris)
{
    const int n = static_cast<int>(poly.Size());
    if (n < 3 || n > 256) { return; }
    int remaining[256];
    int m = n;
    for (int i = 0; i < n; i++) { remaining[i] = i; }

    int guard = n * n + 1;
    while (m > 3 && guard-- > 0) {
        bool clipped = false;
        for (int i = 0; i < m; i++) {
            const int i0 = remaining[(i + m - 1) % m];
            const int i1 = remaining[i];
            const int i2 = remaining[(i + 1) % m];
            const Vec2 a = poly[i0], b = poly[i1], c = poly[i2];
            if (ProfileCross2(a, b, c) <= 0.0f) { continue; }
            bool ear = true;
            for (int k = 0; k < m; k++) {
                const int ik = remaining[k];
                if (ik == i0 || ik == i1 || ik == i2) { continue; }
                if (ProfilePointInTri(poly[ik], a, b, c)) { ear = false; break; }
            }
            if (!ear) { continue; }
            outTris.PushBack(static_cast<uint32_t>(i0));
            outTris.PushBack(static_cast<uint32_t>(i1));
            outTris.PushBack(static_cast<uint32_t>(i2));
            for (int k = i; k < m - 1; k++) { remaining[k] = remaining[k + 1]; }
            m--;
            clipped = true;
            break;
        }
        if (!clipped) { break; }
    }
    if (m == 3) {
        outTris.PushBack(static_cast<uint32_t>(remaining[0]));
        outTris.PushBack(static_cast<uint32_t>(remaining[1]));
        outTris.PushBack(static_cast<uint32_t>(remaining[2]));
    }
}

static void BuildSplineProfile(const Engine::SplineParams& p, Core::Vector<SplineProfilePoint>& ring)
{
    auto push = [&](Vec2 pos, Vec2 n) { ring.PushBack({pos, glm::normalize(n)}); };
    // Expand a CCW corner list into hard-edged ring points (one outward normal per edge).
    auto hardEdges = [&](const Vec2* corners, int n) {
        for (int i = 0; i < n; i++) {
            const Vec2 a = corners[i];
            const Vec2 b = corners[(i + 1) % n];
            Vec2 dir = b - a;
            const float len = glm::length(dir);
            if (len < 1e-8f) { continue; }
            dir /= len;
            push(a, {dir.y, -dir.x});
            push(b, {dir.y, -dir.x});
        }
    };

    const float hw = p.profile.width * 0.5f;
    const float hh = p.profile.height * 0.5f;
    const float t = glm::clamp(p.profile.thickness, 0.001f, glm::min(hw, hh));

    switch (p.profile.type) {
        case Engine::SplineProfileType::Rectangle:
        {
            const Vec2 c[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
            hardEdges(c, 4);
            break;
        }
        case Engine::SplineProfileType::RoundedRect:
        {
            const float cr = glm::clamp(p.profile.cornerRadius, 0.0f, std::min(hw, hh));
            const int cs = std::max(1, p.profile.cornerSegments);
            const float halfPi = glm::half_pi<float>();
            const float pi = glm::pi<float>();
            auto arc = [&](Vec2 ctr, float a0, float a1) {
                for (int k = 0; k <= cs; k++) {
                    const float a = glm::mix(a0, a1, static_cast<float>(k) / static_cast<float>(cs));
                    const Vec2 dir{cosf(a), sinf(a)};
                    push(ctr + cr * dir, dir);
                }
            };
            push({-hw + cr, -hh}, {0, -1}); push({hw - cr, -hh}, {0, -1});
            arc({hw - cr, -hh + cr}, -halfPi, 0.0f);
            push({hw, -hh + cr}, {1, 0}); push({hw, hh - cr}, {1, 0});
            arc({hw - cr, hh - cr}, 0.0f, halfPi);
            push({hw - cr, hh}, {0, 1}); push({-hw + cr, hh}, {0, 1});
            arc({-hw + cr, hh - cr}, halfPi, pi);
            push({-hw, hh - cr}, {-1, 0}); push({-hw, -hh + cr}, {-1, 0});
            arc({-hw + cr, -hh + cr}, pi, 1.5f * pi);
            break;
        }
        case Engine::SplineProfileType::IBeam:
        {
            const float web = t * 0.5f;
            const Vec2 c[12] = {
                {-hw, -hh}, {hw, -hh}, {hw, -hh + t}, {web, -hh + t},
                {web, hh - t}, {hw, hh - t}, {hw, hh}, {-hw, hh},
                {-hw, hh - t}, {-web, hh - t}, {-web, -hh + t}, {-hw, -hh + t}
            };
            hardEdges(c, 12);
            break;
        }
        case Engine::SplineProfileType::UChannel:
        {
            const Vec2 c[8] = {
                {-hw, -hh}, {hw, -hh}, {hw, hh}, {hw - t, hh},
                {hw - t, -hh + t}, {-hw + t, -hh + t}, {-hw + t, hh}, {-hw, hh}
            };
            hardEdges(c, 8);
            break;
        }
        case Engine::SplineProfileType::LAngle:
        {
            const Vec2 c[6] = {
                {-hw, -hh}, {hw, -hh}, {hw, -hh + t}, {-hw + t, -hh + t}, {-hw + t, hh}, {-hw, hh}
            };
            hardEdges(c, 6);
            break;
        }
        case Engine::SplineProfileType::RailHead:
        {
            const float web = t * 0.5f;
            const float headHW = hw * 0.5f;
            const float headT = glm::min(hh * 0.5f, t * 1.5f);
            const Vec2 c[12] = {
                {-hw, -hh}, {hw, -hh}, {hw, -hh + t}, {web, -hh + t},
                {web, hh - headT}, {headHW, hh - headT}, {headHW, hh}, {-headHW, hh},
                {-headHW, hh - headT}, {-web, hh - headT}, {-web, -hh + t}, {-hw, -hh + t}
            };
            hardEdges(c, 12);
            break;
        }
        case Engine::SplineProfileType::Handrail:
        {
            // Vertical stadium / rounded grip: straight sides + semicircular top and bottom.
            const int cs = std::max(2, p.profile.cornerSegments);
            const float r = glm::min(hw, hh);
            const float pi = glm::pi<float>();
            const float straight = glm::max(0.0f, hh - r);
            auto arc = [&](Vec2 ctr, float a0, float a1) {
                for (int k = 0; k <= cs; k++) {
                    const float a = glm::mix(a0, a1, static_cast<float>(k) / static_cast<float>(cs));
                    const Vec2 dir{cosf(a), sinf(a)};
                    push(ctr + r * dir, dir);
                }
            };
            push({r, -straight}, {1, 0}); push({r, straight}, {1, 0});
            arc({0, straight}, 0.0f, pi);
            push({-r, straight}, {-1, 0}); push({-r, -straight}, {-1, 0});
            arc({0, -straight}, pi, 2.0f * pi);
            break;
        }
        case Engine::SplineProfileType::Tube:
        default:
        {
            const int n = std::max(3, p.sides);
            const float r = p.radius;
            for (int j = 0; j < n; j++) {
                const float a = glm::two_pi<float>() * static_cast<float>(j) / static_cast<float>(n);
                const Vec2 dir{cosf(a), sinf(a)};
                push(r * dir, dir);
            }
            break;
        }
    }
}

bool ProceduralModelLoadSlot::GenerateSpline(const Engine::SplineParams& p)
{
    const int N = static_cast<int>(p.spline.points.Size());
    if (N < 2) return false;

    const bool bClosed = p.spline.bClosed;
    const int segs = std::max(1, p.segmentsPerSpan);
    const int totalSpans = bClosed ? N : N - 1;
    const int totalRings = bClosed ? totalSpans * segs : totalSpans * segs + 1;

    auto catmullScalar = [](float v0, float v1, float v2, float v3, float t) -> float {
        return 0.5f * ((2.0f * v1) + (-v0 + v2) * t + (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * (t * t) + (-v0 + 3.0f * v1 - 3.0f * v2 + v3) * (t * t * t));
    };

    auto getRollCP = [&](int i) -> float {
        const int nR = static_cast<int>(p.spline.rolls.Size());
        if (nR == 0) { return 0.0f; }
        if (bClosed) { return p.spline.rolls[((i % nR) + nR) % nR]; }
        return p.spline.rolls[std::clamp(i, 0, nR - 1)];
    };

    struct Frame
    {
        Vec3 pos;
        Vec3 tangent;
        Vec3 right;
        Vec3 up;
        Vec3 rRight;
        Vec3 rUp;
    };
    Core::HeapArray<Frame> frames(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(totalRings));
    Core::HeapArray<float> perRingRoll(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, static_cast<size_t>(totalRings));
    int frameCount = 0;

    for (int i = 0; i < totalRings; i++) {
        int span;
        float t;
        if (!bClosed && i == totalRings - 1) {
            span = N - 2;
            t = 1.0f;
        }
        else {
            span = i / segs;
            t = static_cast<float>(i % segs) / static_cast<float>(segs);
        }

        const int nextSpan = bClosed ? (span + 1) % N : span + 1;
        Vec3 tang = p.spline.EvaluateTangent(span, nextSpan, t);
        if (glm::length(tang) < 1e-6f) { tang = frameCount == 0 ? Vec3{0, 0, 1} : frames[frameCount - 1].tangent; }
        float roll;
        if (p.spline.mode == Engine::SplineMode::Linear) {
            roll = glm::mix(getRollCP(span), getRollCP(nextSpan), t);
        }
        else {
            roll = catmullScalar(getRollCP(span - 1), getRollCP(span), getRollCP(nextSpan), getRollCP(nextSpan + 1), t);
        }
        perRingRoll[i] = roll;
        frames[i] = {p.spline.EvaluatePosition(span, nextSpan, t), glm::normalize(tang), {}, {}, {}, {}};
        frameCount++;
    }

    // Parallel transport frames
    {
        Vec3 worldUp = {0, 1, 0};
        if (glm::abs(glm::dot(frames[0].tangent, worldUp)) > 0.99f) worldUp = {1, 0, 0};
        frames[0].right = glm::normalize(glm::cross(worldUp, frames[0].tangent));
        frames[0].up = glm::normalize(glm::cross(frames[0].tangent, frames[0].right));
        for (int i = 1; i < totalRings; i++) {
            Vec3 axis = glm::cross(frames[i - 1].tangent, frames[i].tangent);
            float axisLen = glm::length(axis);
            if (axisLen < 1e-6f) {
                frames[i].right = frames[i - 1].right;
            }
            else {
                float cosA = glm::clamp(glm::dot(frames[i - 1].tangent, frames[i].tangent), -1.0f, 1.0f);
                glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::acos(cosA), axis / axisLen);
                frames[i].right = glm::normalize(Vec3(rot * glm::vec4(frames[i - 1].right, 0.0f)));
            }
            frames[i].up = glm::normalize(glm::cross(frames[i].tangent, frames[i].right));
        }
    }

    // Apply per-ring roll (base rollAngle + interpolated per-point roll)
    for (int i = 0; i < totalRings; i++) {
        float totalRoll = glm::radians(p.rollAngle + perRingRoll[i]);
        float cr = glm::cos(totalRoll), sr = glm::sin(totalRoll);
        frames[i].rRight = cr * frames[i].right + sr * frames[i].up;
        frames[i].rUp = -sr * frames[i].right + cr * frames[i].up;
    }

    Core::Vector<Engine::FullVertex> vertices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);

    // Cross-section profile (closed loop of pos+normal in the frame plane), swept along the path.
    Core::Vector<SplineProfilePoint> ring(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    BuildSplineProfile(p, ring);
    const int ringSize = static_cast<int>(ring.Size());
    if (ringSize < 3) { return false; }

    // Cap polygon + triangulation (ear-clipped so concave profiles cap correctly). Built once, reused per rail/end.
    Core::Vector<Vec2> capOutline(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    Core::Vector<uint32_t> capTris(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel);
    if (p.bCaps && !bClosed) {
        ProfileOutline(ring, capOutline);
        TriangulateProfileCap(capOutline, capTris);
    }

    const int railCount = p.bDualPath ? 2 : 1;
    const float halfSpacing = p.dualPathSpacing * 0.5f;

    for (int rail = 0; rail < railCount; rail++) {
        const float offsetSign = (railCount == 1) ? 0.0f : (rail == 0 ? -1.0f : 1.0f);
        const auto baseVertex = static_cast<uint32_t>(vertices.Size());

        for (int i = 0; i < totalRings; i++) {
            const Frame& f = frames[i];
            const Vec3 center = f.pos + offsetSign * halfSpacing * f.rRight;
            const float vCoord = (totalRings > 1) ? static_cast<float>(i) / static_cast<float>(totalRings - 1) : 0.0f;
            for (int j = 0; j < ringSize; j++) {
                const SplineProfilePoint& pp = ring[j];
                Engine::FullVertex v{};
                v.position = center + pp.pos.x * f.rRight + pp.pos.y * f.rUp;
                v.normal = glm::normalize(pp.normal.x * f.rRight + pp.normal.y * f.rUp);
                v.uv = {static_cast<float>(j) / static_cast<float>(ringSize), vCoord};
                v.tangent = {f.rRight.x, f.rRight.y, f.rRight.z, 1.0f};
                v.color = {1, 1, 1, 1};
                vertices.PushBack(v);
            }
        }

        const int stitchCount = bClosed ? totalRings : totalRings - 1;
        for (int i = 0; i < stitchCount; i++) {
            const int nextRing = bClosed ? (i + 1) % totalRings : i + 1;
            for (int j = 0; j < ringSize; j++) {
                const int jn = (j + 1) % ringSize;
                uint32_t a0 = baseVertex + i * ringSize + j, a1 = baseVertex + i * ringSize + jn;
                uint32_t b0 = baseVertex + nextRing * ringSize + j, b1 = baseVertex + nextRing * ringSize + jn;
                indices.PushBack(a0);
                indices.PushBack(a1);
                indices.PushBack(b0);
                indices.PushBack(a1);
                indices.PushBack(b1);
                indices.PushBack(b0);
            }
        }

        // Flat end caps. CCW outline triangles face +tangent, so the front cap reverses winding.
        if (p.bCaps && !bClosed && !capTris.IsEmpty()) {
            const int outlineSize = static_cast<int>(capOutline.Size());
            auto addCap = [&](int ringIdx, Vec3 capNormal, bool reverse) {
                const Frame& f = frames[ringIdx];
                const Vec3 center = f.pos + offsetSign * halfSpacing * f.rRight;
                const auto capBase = static_cast<uint32_t>(vertices.Size());
                for (int k = 0; k < outlineSize; k++) {
                    const Vec2& o = capOutline[k];
                    Engine::FullVertex v{};
                    v.position = center + o.x * f.rRight + o.y * f.rUp;
                    v.normal = capNormal;
                    v.uv = {o.x, o.y};
                    v.tangent = {f.rRight.x, f.rRight.y, f.rRight.z, 1.0f};
                    v.color = {1, 1, 1, 1};
                    vertices.PushBack(v);
                }
                for (size_t ti = 0; ti + 3 <= capTris.Size(); ti += 3) {
                    const uint32_t a = capBase + capTris[ti];
                    const uint32_t b = capBase + capTris[ti + 1];
                    const uint32_t c = capBase + capTris[ti + 2];
                    if (reverse) {
                        indices.PushBack(a);
                        indices.PushBack(c);
                        indices.PushBack(b);
                    }
                    else {
                        indices.PushBack(a);
                        indices.PushBack(b);
                        indices.PushBack(c);
                    }
                }
            };
            addCap(0, -frames[0].tangent, true);
            addCap(totalRings - 1, frames[totalRings - 1].tangent, false);
        }
    }

    if (p.bDualPath && p.bCrossPlanks) {
        constexpr float plankThickness = 0.1f;
        const int plankInterval = std::max(1, p.crossPlankInterval);

        auto makeVert = [](Vec3 pos, Vec3 normal, Vec3 tan, float u, float v) {
            Engine::FullVertex vert{};
            vert.position = pos;
            vert.normal = normal;
            vert.uv = {u, v};
            vert.tangent = {tan.x, tan.y, tan.z, 1.0f};
            vert.color = {1, 1, 1, 1};
            return vert;
        };

        const int plankMaxRing = bClosed ? totalRings : totalRings - 1;
        for (int i = 0; i < plankMaxRing; i += plankInterval) {
            const Frame& f0 = frames[i];
            const Frame& f1 = frames[bClosed ? (i + 1) % totalRings : i + 1];

            // 8 corners of the plank box
            const float plankTop = p.radius + p.crossPlankHeight;
            const float plankBot = plankTop - plankThickness;
            const Vec3 tl0 = f0.pos - halfSpacing * f0.rRight + plankTop * f0.rUp;
            const Vec3 tr0 = f0.pos + halfSpacing * f0.rRight + plankTop * f0.rUp;
            const Vec3 bl0 = f0.pos - halfSpacing * f0.rRight + plankBot * f0.rUp;
            const Vec3 br0 = f0.pos + halfSpacing * f0.rRight + plankBot * f0.rUp;
            const Vec3 tl1 = f1.pos - halfSpacing * f1.rRight + plankTop * f1.rUp;
            const Vec3 tr1 = f1.pos + halfSpacing * f1.rRight + plankTop * f1.rUp;
            const Vec3 bl1 = f1.pos - halfSpacing * f1.rRight + plankBot * f1.rUp;
            const Vec3 br1 = f1.pos + halfSpacing * f1.rRight + plankBot * f1.rUp;

            auto base = static_cast<uint32_t>(vertices.Size());

            // Top face (normal = rUp)
            vertices.PushBack(makeVert(tl0, f0.rUp, f0.rRight, 0, 0));
            vertices.PushBack(makeVert(tr0, f0.rUp, f0.rRight, 1, 0));
            vertices.PushBack(makeVert(tl1, f1.rUp, f1.rRight, 0, 1));
            vertices.PushBack(makeVert(tr1, f1.rUp, f1.rRight, 1, 1));
            // Bottom face (normal = -rUp)
            vertices.PushBack(makeVert(bl0, -f0.rUp, f0.rRight, 0, 0));
            vertices.PushBack(makeVert(br0, -f0.rUp, f0.rRight, 1, 0));
            vertices.PushBack(makeVert(bl1, -f1.rUp, f1.rRight, 0, 1));
            vertices.PushBack(makeVert(br1, -f1.rUp, f1.rRight, 1, 1));
            // Front face (normal = -tangent)
            vertices.PushBack(makeVert(tl0, -f0.tangent, f0.rRight, 0, 1));
            vertices.PushBack(makeVert(tr0, -f0.tangent, f0.rRight, 1, 1));
            vertices.PushBack(makeVert(bl0, -f0.tangent, f0.rRight, 0, 0));
            vertices.PushBack(makeVert(br0, -f0.tangent, f0.rRight, 1, 0));
            // Back face (normal = +tangent)
            vertices.PushBack(makeVert(tl1, f1.tangent, f1.rRight, 0, 1));
            vertices.PushBack(makeVert(tr1, f1.tangent, f1.rRight, 1, 1));
            vertices.PushBack(makeVert(bl1, f1.tangent, f1.rRight, 0, 0));
            vertices.PushBack(makeVert(br1, f1.tangent, f1.rRight, 1, 0));
            // Left face (normal = -rRight)
            vertices.PushBack(makeVert(tl0, -f0.rRight, f0.tangent, 0, 1));
            vertices.PushBack(makeVert(tl1, -f1.rRight, f1.tangent, 1, 1));
            vertices.PushBack(makeVert(bl0, -f0.rRight, f0.tangent, 0, 0));
            vertices.PushBack(makeVert(bl1, -f1.rRight, f1.tangent, 1, 0));
            // Right face (normal = +rRight)
            vertices.PushBack(makeVert(tr0, f0.rRight, f0.tangent, 0, 1));
            vertices.PushBack(makeVert(tr1, f1.rRight, f1.tangent, 1, 1));
            vertices.PushBack(makeVert(br0, f0.rRight, f0.tangent, 0, 0));
            vertices.PushBack(makeVert(br1, f1.rRight, f1.tangent, 1, 0));

            // 6 faces x 2 triangles each
            // Faces 0 (top), 3 (back), 4 (left) need flipped winding
            for (int face = 0; face < 6; face++) {
                uint32_t b = base + face * 4;
                if (face == 0 || face == 3 || face == 4) {
                    indices.PushBack(b);
                    indices.PushBack(b + 2);
                    indices.PushBack(b + 1);
                    indices.PushBack(b + 1);
                    indices.PushBack(b + 2);
                    indices.PushBack(b + 3);
                }
                else {
                    indices.PushBack(b);
                    indices.PushBack(b + 1);
                    indices.PushBack(b + 2);
                    indices.PushBack(b + 1);
                    indices.PushBack(b + 3);
                    indices.PushBack(b + 2);
                }
            }
        }
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::AllocateGPUResources() const
{
    struct BufferAlloc
    {
        std::mutex* mutex;
        OffsetAllocator::Allocator* allocator;
        OffsetAllocator::Allocation* allocation;
    };

    Core::InlineVector<BufferAlloc, 6> allocated;

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
            SPDLOG_ERROR("[ProceduralModelLoadSlot] Not enough space in {}", name);
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

    return true;
}

void ProceduralModelLoadSlot::PrepareUploadData()
{
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
            primitiveIndex.index += primitiveOffsetCount;
        }
    } {
        Core::HeapArray<Engine::MeshInformation>& dst = outputModel->modelData.meshes;
        assert(!dst.IsAllocated() && "modelData.meshes was found to be allocated (memory leak)");
        dst = Core::HeapArray<Engine::MeshInformation>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.allMeshes.Size());
        for (size_t i = 0; i < rawData.allMeshes.Size(); ++i) {
            dst[i] = rawData.allMeshes[i];
        }
    }

    // No nodes for procedural
    /*{
        Core::HeapArray<Engine::Node>& dst = outputModel->modelData.nodes;
        assert(!dst.IsAllocated() && "modelData.nodes was found to be allocated (memory leak)");
        dst = Core::HeapArray<Engine::Node>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.nodes.Size());
        for (size_t i = 0; i < rawData.nodes.Size(); ++i) {
            dst[i] = rawData.nodes[i];
        }
    }*/

    // No materials for procedural
    /*{
        Core::HeapArray<Engine::Material>& dst = outputModel->modelData.materials;
        assert(!dst.IsAllocated() && "modelData.materials was found to be allocated (memory leak)");
        dst = Core::HeapArray<Engine::Material>(&memoryManager->Assets(), Core::AllocTag::AssetModel, rawData.materials.Size());
        for (size_t i = 0; i < rawData.materials.Size(); ++i) {
            dst[i] = rawData.materials[i];
        }
    }*/
}

void ProceduralModelLoadSlot::UploadGeometry(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
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
            } else {
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

    VkBufferMemoryBarrier2 releaseBarriers[7];
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

void ProceduralModelLoadSlot::BuildBLAS(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("BuildBLAS");

    const uint32_t primitiveCount = static_cast<uint32_t>(rawData.primitives.Size());
    const VkDeviceAddress vertexBase = resourceManager->megaVertexPositionBuffer.address + outputModel->modelData.vertexPositionAllocation.offset;
    const VkDeviceAddress indexBase = resourceManager->megaIndexBuffer.address + outputModel->modelData.indexAllocation.offset;

    if (!context->bMaintenance9Enabled) {
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
    Render::AllocatedBuffer blasScratch{};

    const int32_t meshCount = static_cast<int32_t>(outputModel->modelData.meshes.Size());
    for (int32_t j = 0; j < meshCount; ++j) {
        Engine::MeshInformation& mesh = outputModel->modelData.meshes[j];
        const int32_t meshPrimitiveCount = static_cast<int32_t>(mesh.primitiveProperties.Size());

        for (int32_t i = 0; i < meshPrimitiveCount; ++i) {
            Engine::PrimitiveProperty& props = mesh.primitiveProperties[i];
            uint32_t primitiveOffsetCount = outputModel->modelData.primitiveAllocation.offset / sizeof(Primitive);
            uint32_t realPrimitiveIndex = props.index - primitiveOffsetCount;
            const Primitive& prim = rawData.primitives[realPrimitiveIndex];

            const uint32_t indexStart = prim.indexOffset;
            const uint32_t indexEnd = (realPrimitiveIndex + 1 < primitiveCount) ? rawData.primitives[realPrimitiveIndex + 1].indexOffset : static_cast<uint32_t>(rawData.indices.Size());
            const uint32_t triCount = (indexEnd - indexStart) / 3;

            VkAccelerationStructureGeometryKHR geom{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            auto& tri = geom.geometry.triangles;
            tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            tri.vertexFormat = VK_FORMAT_R16G16B16A16_UNORM;
            tri.vertexData.deviceAddress = vertexBase;
            tri.vertexStride = sizeof(VertexPosition);
            tri.maxVertex = static_cast<uint32_t>(rawData.vertices.Size()) - 1;
            tri.indexType = VK_INDEX_TYPE_UINT32;
            tri.indexData.deviceAddress = indexBase + indexStart * sizeof(uint32_t);
            tri.transformData.deviceAddress = stagingBuffer.address + realPrimitiveIndex * sizeof(VkTransformMatrixKHR);

            VkAccelerationStructureBuildRangeInfoKHR rangeInfo{.primitiveCount = triCount};
            uint32_t primCount = triCount;

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geom;

            VkAccelerationStructureBuildSizesInfoKHR sizeInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            vkGetAccelerationStructureBuildSizesKHR(context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount, &sizeInfo);

            const VkDeviceSize alignedASSize = (sizeInfo.accelerationStructureSize + 255ull) & ~255ull;
            {
                std::lock_guard lock(resourceManager->blasBufferAllocatorMutex);
                props.blasAllocation = resourceManager->blasBufferAllocator.allocate(alignedASSize);
            }
            if (props.blasAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                SPDLOG_ERROR("[ProceduralModelLoadSlot] No space in mega BLAS buffer for primitive {} of mesh {} of {}", i, j, outputModel->name.c_str());
                return;
            }

            VkAccelerationStructureCreateInfoKHR createInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            createInfo.buffer = resourceManager->megaBLASBuffer.handle;
            createInfo.offset = props.blasAllocation.offset;
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            VkAccelerationStructureKHR blas{};
            VK_CHECK(vkCreateAccelerationStructureKHR(context->device, &createInfo, nullptr, &blas));
            props.blasHandle = reinterpret_cast<uint64_t>(blas);

            VkAccelerationStructureDeviceAddressInfoKHR addrInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
            addrInfo.accelerationStructure = blas;
            props.blasDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(context->device, &addrInfo);

            const VkDeviceSize scratchSize = (sizeInfo.buildScratchSize + scratchAlignment - 1ull) & ~(scratchAlignment - 1ull);
            if (blasScratch.size < scratchSize) {
                blasScratch = {};
                VkBufferCreateInfo scratchBufInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                scratchBufInfo.size = scratchSize;
                scratchBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                VmaAllocationCreateInfo scratchAllocInfo{};
                scratchAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                blasScratch = Render::AllocatedBuffer::CreateAllocatedBufferAligned(context, scratchBufInfo, scratchAllocInfo, scratchAlignment);
            }

            buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfo.dstAccelerationStructure = blas;
            buildInfo.scratchData.deviceAddress = blasScratch.address;

            const VkAccelerationStructureBuildRangeInfoKHR* pRangePtr = &rangeInfo;
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangePtr);

            const bool isLast = (j == meshCount - 1) && (i == meshPrimitiveCount - 1);
            submitAndWait(!isLast);
        }
    }
}
} // AssetLoad
