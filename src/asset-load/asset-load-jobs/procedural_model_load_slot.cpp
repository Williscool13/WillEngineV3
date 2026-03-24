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

#include "par/par_shapes.h"
#include "par/par_shapes_ext.h"
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
               }, params);
    return bSuccess;
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
    std::vector<Vertex> vertices(merged->npoints);
    for (int32_t i = 0; i < merged->npoints; ++i) {
        const glm::vec3 pos = {merged->points[i * 3 + 0], merged->points[i * 3 + 1], merged->points[i * 3 + 2]};
        const glm::vec3 n = merged->normals ? glm::vec3(merged->normals[i * 3 + 0], merged->normals[i * 3 + 1], merged->normals[i * 3 + 2]) : glm::vec3(0, 1, 0);

        // World-space triplanar UV (1 unit = 1 world unit)
        const glm::vec3 absN = glm::abs(n);
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
        vertices[i].texcoordU = u;
        vertices[i].texcoordV = v;
        vertices[i].tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    std::vector<uint32_t> indices(merged->ntriangles * 3);
    for (int32_t i = 0; i < merged->ntriangles * 3; ++i) {
        indices[i] = static_cast<uint32_t>(merged->triangles[i]);
    }

    par_shapes_free_mesh(merged);

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::FinalizeGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    if (vertices.empty() || indices.empty()) return false;

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

    // Build meshlets
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
    auto vertexOffset = static_cast<uint32_t>(rawData.vertices.size());
    auto meshletVertexOffset = static_cast<uint32_t>(rawData.meshletVertices.size());
    auto meshletTriangleOffset = static_cast<uint32_t>(rawData.meshletTriangles.size());
    auto meshletBaseOffset = static_cast<uint32_t>(rawData.meshlets.size());

    rawData.vertices.insert(rawData.vertices.end(), vertices.begin(), vertices.end());

    auto indexOffset = static_cast<uint32_t>(rawData.indices.size());
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

    auto meshletCount = static_cast<uint32_t>(meshlets.size());
    Primitive primitiveData{};
    primitiveData.meshletOffset = glm::ivec4(static_cast<int>(meshletBaseOffset));
    primitiveData.meshletCount = glm::ivec4(static_cast<int>(meshletCount));
    primitiveData.boundingSphere = {center, radius};
    primitiveData.bHasTransparent = 0;
    primitiveData.indexOffset = indexOffset;

    Engine::MeshInformation meshInfo;
    meshInfo.name = outputModel->name;
    meshInfo.primitiveProperties.push_back({static_cast<uint32_t>(rawData.primitives.size()), -1});

    rawData.primitives.push_back(primitiveData);
    rawData.allMeshes.push_back(std::move(meshInfo)); {
        std::vector<glm::vec3> positions;
        positions.reserve(vertices.size());
        for (const auto& v : vertices) positions.push_back(v.position); {
            Engine::StaticModel::PhysicsCache cache;
            cache.positions = positions;
            cache.indices = indices;

            constexpr size_t kSimplifyThreshold = 1500;
            constexpr size_t kSimplifyFloor = 1500;
            constexpr float kSimplifyRatio = 0.15f;
            constexpr float kSimplifyError = 0.01f;
            if (cache.indices.size() > kSimplifyThreshold) {
                const size_t target = std::max(kSimplifyFloor, static_cast<size_t>(static_cast<float>(cache.indices.size()) * kSimplifyRatio));
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

        outputModel->bounds = Engine::StaticModel::ComputeBounds(positions, &indices);
    }

    return true;
}

bool ProceduralModelLoadSlot::GenerateBox(const Engine::BoxParams& p)
{
    ZoneScopedN("GenerateBox");

    const float sx = p.sizeX, sy = p.sizeY, sz = p.sizeZ;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(24);
    indices.reserve(36);

    auto addFace = [&](glm::vec3 n, glm::vec3 t,
                       glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3,
                       glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3) {
        auto base = static_cast<uint32_t>(vertices.size());
        auto push = [&](glm::vec3 pos, glm::vec2 uv) {
            Vertex v{};
            v.position = pos;
            v.normal = n;
            v.texcoordU = uv.x;
            v.texcoordV = uv.y;
            v.tangent = {t.x, t.y, t.z, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        };
        push(v0, uv0);
        push(v1, uv1);
        push(v2, uv2);
        push(v3, uv3);
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
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

    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        vertices[i].position = {px * r, pz * h - hh, -py * r};
        const float nx = m->normals[i * 3 + 0], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        vertices[i].normal = glm::normalize(glm::vec3{nx, nz, -ny});
        vertices[i].texcoordU = m->tcoords[i * 2 + 0]; // u = height 0→1
        vertices[i].texcoordV = m->tcoords[i * 2 + 1]; // v = angle 0→1
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i)
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
    par_shapes_free_mesh(m);

    if (p.bCapped) {
        // Top cap (+Y): {center, ring[j+1], ring[j]} → +Y ✓
        auto capBase = static_cast<uint32_t>(vertices.size());
        Vertex cv{};
        cv.position = {0, hh, 0};
        cv.normal = {0, 1, 0};
        cv.texcoordU = 0.5f;
        cv.texcoordV = 0.5f;
        cv.tangent = {-1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.push_back(cv);
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * glm::pi<float>();
            Vertex v{};
            v.position = {cosf(angle) * r, hh, sinf(angle) * r};
            v.normal = {0, 1, 0};
            v.texcoordU = -cosf(angle) * 0.5f + 0.5f;
            v.texcoordV = sinf(angle) * 0.5f + 0.5f;
            v.tangent = {-1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
        for (int j = 0; j < N; j++)
            indices.insert(indices.end(), {capBase, capBase + 1 + (j + 1) % N, capBase + 1 + j});

        // Bottom cap (-Y): {center, ring[j], ring[j+1]} → -Y ✓
        capBase = static_cast<uint32_t>(vertices.size());
        cv.position = {0, -hh, 0};
        cv.normal = {0, -1, 0};
        vertices.push_back(cv);
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * glm::pi<float>();
            Vertex v{};
            v.position = {cosf(angle) * r, -hh, sinf(angle) * r};
            v.normal = {0, -1, 0};
            v.texcoordU = cosf(angle) * 0.5f + 0.5f;
            v.texcoordV = sinf(angle) * 0.5f + 0.5f;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
        for (int j = 0; j < N; j++)
            indices.insert(indices.end(), {capBase, capBase + 1 + j, capBase + 1 + (j + 1) % N});
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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const auto pi = glm::pi<float>();
    const float pi2 = 2.0f * pi;

    // Helper: add a ring of (N+1) vertices at given y, ringR, outward normal y-component
    auto addRing = [&](float ringY, float ringR, float nY, float nXZScale) {
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * pi2;
            const float cx = cosf(angle), cz = sinf(angle);
            Vertex v{};
            v.position = {cx * ringR, ringY, cz * ringR};
            v.normal = {cx * nXZScale, nY, cz * nXZScale};
            v.texcoordU = 1.0f - static_cast<float>(j) / static_cast<float>(N);
            v.texcoordV = (ringY + bhh + r) / (2.0f * (bhh + r)); // 0=bottom, 1=top
            v.tangent = {cz, 0, -cx, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
    };

    // Connect two adjacent rings (each has N+1 verts starting at baseA / baseB)
    auto connectRings = [&](uint32_t baseA, uint32_t baseB) {
        for (int j = 0; j < N; j++) {
            uint32_t a0 = baseA + j, a1 = baseA + j + 1;
            uint32_t b0 = baseB + j, b1 = baseB + j + 1;
            indices.insert(indices.end(), {a0, a1, b0, b0, a1, b1});
        }
    };

    // Top pole
    {
        Vertex v{};
        v.position = {0, bhh + r, 0};
        v.normal = {0, 1, 0};
        v.texcoordU = 0.5f;
        v.texcoordV = 1.0f;
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.push_back(v);
    }
    const uint32_t topPole = 0;

    // Top hemisphere rings (ring 1 = near pole, ring HR = equator)
    std::vector<uint32_t> topRingBase(HR + 1);
    for (int i = 1; i <= HR; i++) {
        const float phi = pi * 0.5f * static_cast<float>(i) / static_cast<float>(HR); // 0→pi/2
        topRingBase[i] = static_cast<uint32_t>(vertices.size());
        addRing(r * cosf(phi) + bhh, r * sinf(phi), cosf(phi), sinf(phi));
    }

    // Bottom hemisphere rings (ring 1 = equator, ring HR = near pole)
    std::vector<uint32_t> botRingBase(HR + 1);
    for (int i = 1; i <= HR; i++) {
        const float psi = pi * 0.5f * static_cast<float>(i) / static_cast<float>(HR); // 0→pi/2
        botRingBase[i] = static_cast<uint32_t>(vertices.size());
        addRing(-r * sinf(psi) - bhh, r * cosf(psi), -sinf(psi), cosf(psi));
    }

    // Bottom pole
    const auto botPole = static_cast<uint32_t>(vertices.size()); {
        Vertex v{};
        v.position = {0, -(bhh + r), 0};
        v.normal = {0, -1, 0};
        v.texcoordU = 0.5f;
        v.texcoordV = 0.0f;
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.push_back(v);
    }

    // Top pole fan → first top ring
    for (int j = 0; j < N; j++)
        indices.insert(indices.end(), {topPole, topRingBase[1] + j + 1, topRingBase[1] + j});

    // Top hemisphere strips
    for (int i = 1; i < HR; i++)
        connectRings(topRingBase[i], topRingBase[i + 1]);

    // Body strip (top equator → bottom equator)
    connectRings(topRingBase[HR], botRingBase[1]);

    // Bottom hemisphere strips (stop at HR-1: ring[HR] has ringR=0 and is degenerate)
    for (int i = 1; i < HR - 1; i++)
        connectRings(botRingBase[i], botRingBase[i + 1]);

    // Last non-degenerate bottom ring → bottom pole fan
    const uint32_t lastBot = botRingBase[HR - 1];
    for (int j = 0; j < N; j++)
        indices.insert(indices.end(), {botPole, lastBot + j, lastBot + j + 1});

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

    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        vertices[i].position = {m->points[i * 3 + 0], m->points[i * 3 + 1], m->points[i * 3 + 2]};
        vertices[i].normal = m->normals ? glm::vec3(m->normals[i * 3 + 0], m->normals[i * 3 + 1], m->normals[i * 3 + 2]) : glm::vec3(0, 1, 0);
        vertices[i].texcoordU = m->tcoords ? m->tcoords[i * 2 + 0] : 0.0f;
        vertices[i].texcoordV = m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i)
        indices[i] = static_cast<uint32_t>(m->triangles[i]);

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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Build outer and inner arc contour points (in XY)
    // N=1 special case: rectangular top (3 segments) instead of degenerate zero-height arc.
    // The rectangle extends up by outerR to match the visual "depth" of the arch sides.
    std::vector<glm::vec2> outerArc, innerArc;
    if (N == 1) {
        outerArc = {{outerR, legH}, {outerR, legH + outerR}, {-outerR, legH + outerR}, {-outerR, legH}};
        innerArc = {{innerR, legH}, {innerR, legH + innerR}, {-innerR, legH + innerR}, {-innerR, legH}};
    }
    else {
        outerArc.resize(N + 1);
        innerArc.resize(N + 1);
        for (int i = 0; i <= N; i++) {
            const float theta = pi * static_cast<float>(i) / static_cast<float>(N);
            outerArc[i] = {outerR * cosf(theta), legH + outerR * sinf(theta)};
            innerArc[i] = {innerR * cosf(theta), legH + innerR * sinf(theta)};
        }
    }

    // Outer contour: bottom-right, arc points, bottom-left
    // Inner contour: same structure
    std::vector<glm::vec2> outerC, innerC;
    outerC.emplace_back(outerR, 0);
    for (auto& pt : outerArc) outerC.push_back(pt);
    outerC.emplace_back(-outerR, 0);

    innerC.emplace_back(innerR, 0);
    for (auto& pt : innerArc) innerC.push_back(pt);
    innerC.emplace_back(-innerR, 0);

    const int M = static_cast<int>(outerC.size());

    // Helper: add a flat quad with given normal and basic UV
    auto addQuad = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 n) {
        auto base = static_cast<uint32_t>(vertices.size());
        const glm::vec3 absN = glm::abs(n);
        const bool flipU = (absN.x >= absN.y && absN.x >= absN.z && n.x > 0.0f)
                           || (absN.y >= absN.x && absN.y >= absN.z && n.y > 0.0f)
                           || (absN.z >= absN.x && absN.z >= absN.y && n.z < 0.0f);
        auto push = [&](glm::vec3 pos) {
            Vertex v{};
            v.position = pos;
            v.normal = n;
            v.texcoordU = flipU ? -pos.x : pos.x;
            v.texcoordV = pos.y;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        };
        push(v0);
        push(v1);
        push(v2);
        push(v3);
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
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
    auto addSmoothWallQuad = [&](glm::vec2 A, glm::vec2 B, glm::vec3 nA, glm::vec3 nB, bool reversed) {
        auto base = static_cast<uint32_t>(vertices.size());
        auto push = [&](glm::vec3 pos, glm::vec3 n) {
            Vertex v{};
            v.position = pos;
            v.normal = n;
            v.texcoordU = pos.x;
            v.texcoordV = pos.y;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
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
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };

    // Outer side walls: arc segments use smooth radial normals from (0, legH);
    // legs and rectangular top use flat edge-perpendicular normals.
    for (int i = 0; i < M - 1; i++) {
        const glm::vec2 A = outerC[i], B = outerC[i + 1];
        const bool isArc = (N >= 2) && (i > 0) && (i < M - 2);
        if (isArc) {
            const glm::vec3 nA = glm::normalize(glm::vec3{A.x, A.y - legH, 0.0f});
            const glm::vec3 nB = glm::normalize(glm::vec3{B.x, B.y - legH, 0.0f});
            addSmoothWallQuad(A, B, nA, nB, false);
        }
        else {
            const glm::vec2 d = B - A;
            addQuad({A.x, A.y, 0}, {B.x, B.y, 0}, {B.x, B.y, depth}, {A.x, A.y, depth},
                    glm::normalize(glm::vec3{d.y, -d.x, 0.0f}));
        }
    }

    // Inner side walls: same but inward normals, reversed winding
    for (int i = 0; i < M - 1; i++) {
        const glm::vec2 A = innerC[i], B = innerC[i + 1];
        const bool isArc = (N >= 2) && (i > 0) && (i < M - 2);
        if (isArc) {
            const glm::vec3 nA = glm::normalize(glm::vec3{-A.x, legH - A.y, 0.0f});
            const glm::vec3 nB = glm::normalize(glm::vec3{-B.x, legH - B.y, 0.0f});
            addSmoothWallQuad(A, B, nA, nB, true);
        }
        else {
            const glm::vec2 d = B - A;
            addQuad({B.x, B.y, 0}, {A.x, A.y, 0}, {A.x, A.y, depth}, {B.x, B.y, depth},
                    glm::normalize(glm::vec3{-d.y, d.x, 0.0f}));
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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(18);
    indices.reserve(18);

    auto addQuad = [&](glm::vec3 n, glm::vec3 t,
                       glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3,
                       glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3) {
        auto base = static_cast<uint32_t>(vertices.size());
        auto push = [&](glm::vec3 pos, glm::vec2 uv) {
            Vertex v{};
            v.position = pos;
            v.normal = n;
            v.texcoordU = uv.x;
            v.texcoordV = uv.y;
            v.tangent = {t.x, t.y, t.z, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        };
        push(v0, uv0);
        push(v1, uv1);
        push(v2, uv2);
        push(v3, uv3);
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };

    auto addTri = [&](glm::vec3 n, glm::vec3 t,
                      glm::vec3 v0, glm::vec3 v1, glm::vec3 v2,
                      glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2) {
        auto base = static_cast<uint32_t>(vertices.size());
        auto push = [&](glm::vec3 pos, glm::vec2 uv) {
            Vertex v{};
            v.position = pos;
            v.normal = n;
            v.texcoordU = uv.x;
            v.texcoordV = uv.y;
            v.tangent = {t.x, t.y, t.z, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        };
        push(v0, uv0);
        push(v1, uv1);
        push(v2, uv2);
        indices.insert(indices.end(), {base, base + 1, base + 2});
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
    const glm::vec3 slopeN = glm::normalize(glm::vec3(0.0f, sz, -sy));
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

    // Remap to Y-up: engine = (par.x*r, par.z*h, -par.y*r)
    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const glm::vec3 pos = {px * r, pz * h, -py * r};
        vertices[i].position = pos;
        const float xzRadius = sqrtf(pos.x * pos.x + pos.z * pos.z);
        if (xzRadius > 1e-6f) {
            vertices[i].normal = glm::normalize(glm::vec3{h * pos.x / xzRadius, r, h * pos.z / xzRadius});
        }
        else {
            vertices[i].normal = {0.0f, 1.0f, 0.0f}; // apex
        }
        vertices[i].texcoordU = atan2f(-pos.z, pos.x) / (2.0f * glm::pi<float>()) + 0.5f;
        vertices[i].texcoordV = pos.y / h;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i)
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
    par_shapes_free_mesh(m);

    if (p.bCapped) {
        // Bottom cap (-Y): {center, ring[j], ring[j+1]} → -Y ✓
        auto capBase = static_cast<uint32_t>(vertices.size());
        Vertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.texcoordU = 0.5f;
        cv.texcoordV = 0.5f;
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.push_back(cv);
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * glm::pi<float>();
            Vertex v{};
            v.position = {cosf(angle) * r, 0.0f, sinf(angle) * r};
            v.normal = {0, -1, 0};
            v.texcoordU = cosf(angle) * 0.5f + 0.5f;
            v.texcoordV = sinf(angle) * 0.5f + 0.5f;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
        for (int j = 0; j < N; j++)
            indices.insert(indices.end(), {capBase, capBase + 1 + j, capBase + 1 + (j + 1) % N});
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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Compute arc parameters (general circular arc spanning width w with height hA).
    // Circle: center = (w/2, archStart - c), radius = hA + c, where c = (w²/4 - hA²)/(2·hA).
    // hA=0 → flat top (special-cased). hA=w/2 → semicircle (c=0). hA>w/2 → Gothic arch (c<0).
    float arcC = 0.0f, arcR = 0.0f, thetaStart = 0.0f, thetaEnd = glm::pi<float>();
    glm::vec2 arcCen{w * 0.5f, archStart};
    if (hA > 0.0f) {
        arcC = (w * w * 0.25f - hA * hA) / (2.0f * hA);
        arcCen = {w * 0.5f, archStart - arcC};
        arcR = hA + arcC;
        thetaStart = atan2f(arcC, w * 0.5f); // angle from center to (w, archStart)
        thetaEnd = glm::pi<float>() - thetaStart; // angle to (0, archStart)
    }

    // Build 2D profile polygon, CCW in XY.
    // Profile always closes: last vertex → (0,0) via the left (hinge) edge.
    std::vector<glm::vec2> poly;
    poly.emplace_back(0.0f, 0.0f); // bottom-left (hinge)

    const float seamX = p.bHalf ? w * 0.5f - p.gap : w;
    poly.emplace_back(seamX, 0.0f); // bottom-right / bottom-seam

    if (hA > 0.0f) {
        // Arc: full leaf θ=thetaStart→thetaEnd; half leaf θ from x=seamX→thetaEnd
        // arcFrom = acos(-gap/arcR) keeps the seam edge vertical (gap=0 → π/2 as before)
        const float arcFrom = p.bHalf ? acosf(glm::clamp(-p.gap / arcR, -1.0f, 1.0f)) : thetaStart;
        for (int i = 0; i <= sides; i++) {
            const float t = static_cast<float>(i) / static_cast<float>(sides);
            const float theta = arcFrom + (thetaEnd - arcFrom) * t;
            poly.emplace_back(arcCen.x + arcR * cosf(theta), arcCen.y + arcR * sinf(theta));
        }
        // poly.back() = (0, archStart); closing edge returns to (0,0)
    }
    else {
        // Flat top
        poly.emplace_back(seamX, h);
        poly.emplace_back(0.0f, h);
    }

    // bFlip: mirror about x=0 (door extends to -X) and reverse poly[1..] to maintain CCW
    if (p.bFlip) {
        for (auto& pt : poly) pt.x = -pt.x;
        std::reverse(poly.begin() + 1, poly.end());
    }

    const int M = static_cast<int>(poly.size());

    // Front face (Z=0, normal (0,0,-1)): fan with reversed winding so cross product = -Z
    {
        auto base = static_cast<uint32_t>(vertices.size());
        for (const auto& pt : poly) {
            Vertex v{};
            v.position = {pt.x, pt.y, 0.0f};
            v.normal = {0, 0, -1};
            v.texcoordU = -pt.x;
            v.texcoordV = pt.y;
            v.tangent = {-1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
        for (int i = 1; i + 1 < M; i++)
            indices.insert(indices.end(), {base, base + (uint32_t) (i + 1), base + (uint32_t) i});
    }

    // Back face (Z=d, normal (0,0,1)): forward winding so cross product = +Z
    {
        auto base = static_cast<uint32_t>(vertices.size());
        for (const auto& pt : poly) {
            Vertex v{};
            v.position = {pt.x, pt.y, d};
            v.normal = {0, 0, 1};
            v.texcoordU = pt.x;
            v.texcoordV = pt.y;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
        for (int i = 1; i + 1 < M; i++)
            indices.insert(indices.end(), {base, base + (uint32_t) i, base + (uint32_t) (i + 1)});
    }

    // Arc center may be mirrored by bFlip
    const glm::vec2 effectiveArcCen = p.bFlip ? glm::vec2{-arcCen.x, arcCen.y} : arcCen;

    // Returns true if pt lies on the arc (within tolerance)
    auto isOnArc = [&](glm::vec2 pt) -> bool {
        if (hA <= 0.0f || arcR < 1e-6f) return false;
        return fabsf(glm::length(pt - effectiveArcCen) - arcR) < 1e-4f;
    };

    // Perimeter walls: flat edges use edge-perpendicular normal; arc edges use smooth radial normals.
    for (int i = 0; i < M; i++) {
        const glm::vec2 A = poly[i];
        const glm::vec2 B = poly[(i + 1) % M];
        const float edgeLen = glm::length(B - A);
        if (edgeLen < 1e-6f) continue;

        glm::vec3 nA, nB;
        if (isOnArc(A) && isOnArc(B)) {
            nA = glm::normalize(glm::vec3{A.x - effectiveArcCen.x, A.y - effectiveArcCen.y, 0.0f});
            nB = glm::normalize(glm::vec3{B.x - effectiveArcCen.x, B.y - effectiveArcCen.y, 0.0f});
        }
        else {
            nA = nB = glm::normalize(glm::vec3{B.y - A.y, A.x - B.x, 0.0f});
        }

        auto base = static_cast<uint32_t>(vertices.size());
        auto push = [&](glm::vec3 pos, glm::vec3 n, glm::vec2 uv) {
            Vertex v{};
            v.position = pos;
            v.normal = n;
            v.texcoordU = uv.x;
            v.texcoordV = uv.y;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        };
        push({A.x, A.y, 0.0f}, nA, {0.0f, 0.0f});
        push({B.x, B.y, 0.0f}, nB, {edgeLen, 0.0f});
        push({B.x, B.y, d}, nB, {edgeLen, d});
        push({A.x, A.y, d}, nA, {0.0f, d});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
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

    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1];
        vertices[i].position = {(px - 0.5f) * sx, 0.0f, (py - 0.5f) * sz};
        vertices[i].normal = {0.0f, 1.0f, 0.0f};
        vertices[i].texcoordU = m->tcoords ? m->tcoords[i * 2 + 0] * sx : px * sx;
        vertices[i].texcoordV = m->tcoords ? m->tcoords[i * 2 + 1] * sz : py * sz;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    // Flip each triangle: XY→XZ remap flips handedness, reversing winding.
    std::vector<uint32_t> indices(m->ntriangles * 3);
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

    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const glm::vec3 pos = {px * r, pz * r, -py * r};
        vertices[i].position = pos;
        const float nx = m->normals[i * 3 + 0], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        vertices[i].normal = glm::normalize(glm::vec3{nx, nz, -ny});
        // Spherical UV: u = longitude 0→1, v = latitude 0(south)→1(north)
        vertices[i].texcoordU = 0.5f - atan2f(-pos.z, pos.x) / (2.0f * pi);
        vertices[i].texcoordV = pos.y / r * 0.5f + 0.5f;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i)
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
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

    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const glm::vec3 pos = glm::normalize(glm::vec3{
                                  m->points[i * 3 + 0], m->points[i * 3 + 1], m->points[i * 3 + 2]
                              }) * r;
        vertices[i].position = pos;
        vertices[i].normal = glm::normalize(pos);
        vertices[i].texcoordU = 0.5f - atan2f(pos.z, pos.x) / (2.0f * pi);
        vertices[i].texcoordV = asinf(glm::clamp(pos.y / r, -1.0f, 1.0f)) / pi + 0.5f;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i)
        indices[i] = static_cast<uint32_t>(m->triangles[i]);
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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    {
        Vertex v{};
        v.position = {0, r, 0};
        v.normal = {0, 1, 0};
        v.texcoordU = 0.5f;
        v.texcoordV = 1.0f;
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.push_back(v);
    }
    const uint32_t pole = 0;

    std::vector<uint32_t> ringBase(HR + 1);
    for (int i = 1; i <= HR; i++) {
        const float phi = pi * 0.5f * (1.0f - static_cast<float>(i) / HR);
        ringBase[i] = static_cast<uint32_t>(vertices.size());
        const float ringR = r * cosf(phi);
        const float ringY = r * sinf(phi);
        const float nY = sinf(phi), nXZ = cosf(phi);
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / N * 2.0f * pi;
            Vertex v{};
            v.position = {cosf(angle) * ringR, ringY, sinf(angle) * ringR};
            v.normal = {cosf(angle) * nXZ, nY, sinf(angle) * nXZ};
            v.texcoordU = static_cast<float>(j) / N;
            v.texcoordV = static_cast<float>(HR - i) / HR;
            v.tangent = {-sinf(angle), 0, cosf(angle), 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
    }

    for (int j = 0; j < N; j++)
        indices.insert(indices.end(), {pole, ringBase[1] + (uint32_t) j + 1, ringBase[1] + (uint32_t) j});

    for (int i = 1; i < HR; i++) {
        for (int j = 0; j < N; j++) {
            uint32_t a0 = ringBase[i] + j, a1 = ringBase[i] + j + 1;
            uint32_t b0 = ringBase[i + 1] + j, b1 = ringBase[i + 1] + j + 1;
            indices.insert(indices.end(), {a0, a1, b0, b0, a1, b1});
        }
    }

    uint32_t capBase = static_cast<uint32_t>(vertices.size()); {
        Vertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.texcoordU = 0.5f;
        cv.texcoordV = 0.5f;
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.push_back(cv);
    }
    for (int j = 0; j < N; j++) {
        const float angle = static_cast<float>(j) / N * 2.0f * pi;
        Vertex v{};
        v.position = {cosf(angle) * r, 0.0f, sinf(angle) * r};
        v.normal = {0, -1, 0};
        v.texcoordU = cosf(angle) * 0.5f + 0.5f;
        v.texcoordV = sinf(angle) * 0.5f + 0.5f;
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        vertices.push_back(v);
    }
    for (int j = 0; j < N; j++)
        indices.insert(indices.end(), {capBase, capBase + 1 + (uint32_t) j, capBase + 1 + (uint32_t) ((j + 1) % N)});

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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto addRingQuads = [&](float radius, glm::vec3 normal_dir, bool outward) {
        // Outer or inner side cylinder wall
        auto base = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * pi;
            const float cx = cosf(angle), cz = sinf(angle);
            glm::vec3 n = outward ? glm::vec3{cx, 0, cz} : glm::vec3{-cx, 0, -cz};
            for (int k = 0; k < 2; k++) {
                Vertex v{};
                v.position = {cx * radius, k == 0 ? -hh : hh, cz * radius};
                v.normal = n;
                v.texcoordU = static_cast<float>(j) / static_cast<float>(N);
                v.texcoordV = k == 0 ? 0.0f : 1.0f;
                v.tangent = {-cz, 0, cx, 1.0f};
                v.color = {1, 1, 1, 1};
                vertices.push_back(v);
            }
        }
        for (int j = 0; j < N; j++) {
            uint32_t a0 = base + j * 2, a1 = base + j * 2 + 1;
            uint32_t b0 = base + (j + 1) * 2, b1 = base + (j + 1) * 2 + 1;
            if (outward) {
                // Outer: CCW from outside
                indices.insert(indices.end(), {a0, a1, b0, a1, b1, b0});
            }
            else {
                // Inner: CCW from inside
                indices.insert(indices.end(), {a0, b0, a1, a1, b0, b1});
            }
        }
    };

    addRingQuads(ro, {}, true); // outer wall
    addRingQuads(ri, {}, false); // inner wall

    // Top annular cap (+Y): fan from outer ring to inner ring
    {
        auto base = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * pi;
            const float cx = cosf(angle), cz = sinf(angle);
            // Outer edge vertex
            Vertex vo{};
            vo.position = {cx * ro, hh, cz * ro};
            vo.normal = {0, 1, 0};
            vo.texcoordU = -cx * ro;
            vo.texcoordV = cz * ro;
            vo.tangent = {-1, 0, 0, 1};
            vo.color = {1, 1, 1, 1};
            vertices.push_back(vo);
            // Inner edge vertex
            Vertex vi{};
            vi.position = {cx * ri, hh, cz * ri};
            vi.normal = {0, 1, 0};
            vi.texcoordU = -cx * ri;
            vi.texcoordV = cz * ri;
            vi.tangent = {-1, 0, 0, 1};
            vi.color = {1, 1, 1, 1};
            vertices.push_back(vi);
        }
        for (int j = 0; j < N; j++) {
            uint32_t oa = base + j * 2, oi = base + j * 2 + 1;
            uint32_t na = base + (j + 1) * 2, ni = base + (j + 1) * 2 + 1;
            indices.insert(indices.end(), {oa, oi, na, oi, ni, na});
        }
    }

    // Bottom annular cap (-Y): reversed winding
    {
        auto base = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / static_cast<float>(N) * 2.0f * pi;
            const float cx = cosf(angle), cz = sinf(angle);
            Vertex vo{};
            vo.position = {cx * ro, -hh, cz * ro};
            vo.normal = {0, -1, 0};
            vo.texcoordU = cx * ro;
            vo.texcoordV = cz * ro;
            vo.tangent = {1, 0, 0, 1};
            vo.color = {1, 1, 1, 1};
            vertices.push_back(vo);
            Vertex vi{};
            vi.position = {cx * ri, -hh, cz * ri};
            vi.normal = {0, -1, 0};
            vi.texcoordU = cx * ri;
            vi.texcoordV = cz * ri;
            vi.tangent = {1, 0, 0, 1};
            vi.color = {1, 1, 1, 1};
            vertices.push_back(vi);
        }
        for (int j = 0; j < N; j++) {
            uint32_t oa = base + j * 2, oi = base + j * 2 + 1;
            uint32_t na = base + (j + 1) * 2, ni = base + (j + 1) * 2 + 1;
            indices.insert(indices.end(), {oa, na, oi, oi, na, ni});
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
    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        // Tetrahedron is already Y-up
        const glm::vec3 pos = glm::vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * r;
        const glm::vec3 n = glm::normalize(glm::vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]});
        const glm::vec3 absN = glm::abs(n);
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
        vertices[i].texcoordU = u;
        vertices[i].texcoordV = v;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) indices[i] = static_cast<uint32_t>(m->triangles[i]);
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
    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const glm::vec3 pos = glm::vec3{px, pz, -py} * r; // Z-up to Y-up
        const float nx = m->normals[i * 3], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        const glm::vec3 n = glm::normalize(glm::vec3{nx, nz, -ny});
        const glm::vec3 absN = glm::abs(n);
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
        vertices[i].texcoordU = u;
        vertices[i].texcoordV = v;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) indices[i] = static_cast<uint32_t>(m->triangles[i]);
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
    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const glm::vec3 pos = glm::vec3{px, pz, -py} * r; // Z-up to Y-up
        const float nx = m->normals[i * 3], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        const glm::vec3 n = glm::normalize(glm::vec3{nx, nz, -ny});
        const glm::vec3 absN = glm::abs(n);
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
        vertices[i].texcoordU = u;
        vertices[i].texcoordV = v;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) indices[i] = static_cast<uint32_t>(m->triangles[i]);
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
    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const glm::vec3 pos = glm::vec3{px, pz, -py} * r; // Z-up to Y-up
        const float nx = m->normals[i * 3], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        const glm::vec3 n = glm::normalize(glm::vec3{nx, nz, -ny});
        const glm::vec3 absN = glm::abs(n);
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
        vertices[i].texcoordU = u;
        vertices[i].texcoordV = v;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) indices[i] = static_cast<uint32_t>(m->triangles[i]);
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateKleinBottle(const Engine::KleinBottleParams& p)
{
    ZoneScopedN("GenerateKleinBottle");

    par_shapes_mesh* m = par_shapes_create_klein_bottle(std::max(3, p.slices), std::max(3, p.stacks));
    if (!m) return false;

    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        vertices[i].position = glm::vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * p.scale;
        vertices[i].normal = m->normals ? glm::normalize(glm::vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]}) : glm::vec3{0, 1, 0};
        vertices[i].texcoordU = m->tcoords ? m->tcoords[i * 2] : 0.0f;
        vertices[i].texcoordV = m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) indices[i] = static_cast<uint32_t>(m->triangles[i]);
    par_shapes_free_mesh(m);
    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateTrefoilKnot(const Engine::TrefoilKnotParams& p)
{
    ZoneScopedN("GenerateTrefoilKnot");

    const float tubeRadius = glm::clamp(p.tubeRadius, 0.5f, 3.0f);
    par_shapes_mesh* m = par_shapes_create_trefoil_knot(std::max(3, p.slices), std::max(3, p.stacks), tubeRadius);
    if (!m) return false;

    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        vertices[i].position = glm::vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * p.scale;
        vertices[i].normal = m->normals ? glm::normalize(glm::vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]}) : glm::vec3{0, 1, 0};
        vertices[i].texcoordU = m->tcoords ? m->tcoords[i * 2] : 0.0f;
        vertices[i].texcoordV = m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f;
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].color = {1, 1, 1, 1};
    }
    std::vector<uint32_t> indices(m->ntriangles * 3);
    for (int i = 0; i < m->ntriangles * 3; ++i) indices[i] = static_cast<uint32_t>(m->triangles[i]);
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
    std::vector<glm::vec3> profile;
    std::vector<glm::vec3> profileN;

    if (h > R) {
        profile.push_back({0.0f, lip + h, 0.0f});
        profileN.push_back({0.0f, 0.0f, 1.0f});
        profile.push_back({0.0f, lip + R, 0.0f});
        profileN.push_back({0.0f, 0.0f, 1.0f});
    }

    for (int i = 0; i <= seg; i++) {
        const float t = static_cast<float>(i) / seg;
        const float angle = pi + t * (pi * 0.5f);
        const float z = R + R * cosf(angle);
        const float y = lip + R + R * sinf(angle);
        profile.push_back({0.0f, y, z});
        profileN.push_back(glm::normalize(glm::vec3{0.0f, -sinf(angle), -cosf(angle)}));
    }

    if (p.bHalfPipe) {
        if (fl > 0.0f) {
            profile.push_back({0.0f, lip, R + fl});
            profileN.push_back({0.0f, 1.0f, 0.0f});
        }

        const float zCenter2 = R + fl;
        for (int i = 0; i <= seg; i++) {
            const float t = static_cast<float>(i) / seg;
            const float angle = -pi * 0.5f + t * (pi * 0.5f);
            const float z = zCenter2 + R * cosf(angle);
            const float y = lip + R + R * sinf(angle);
            profile.push_back({0.0f, y, z});
            profileN.push_back(glm::normalize(glm::vec3{0.0f, -sinf(angle), -cosf(angle)}));
        }

        if (h > R) {
            const float zBack = 2.0f * R + fl;
            profile.push_back({0.0f, lip + R, zBack});
            profileN.push_back({0.0f, 0.0f, -1.0f});
            profile.push_back({0.0f, lip + h, zBack});
            profileN.push_back({0.0f, 0.0f, -1.0f});
        }
    }

    const int profCount = static_cast<int>(profile.size());
    const float totalDepth = p.bHalfPipe ? 2.0f * R + fl : R;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto pushVert = [&](glm::vec3 pos, glm::vec3 n, float u, float v) {
        Vertex vtx{};
        vtx.position = pos;
        vtx.normal = n;
        vtx.texcoordU = u;
        vtx.texcoordV = v;
        vtx.tangent = {1, 0, 0, 1};
        vtx.color = {1, 1, 1, 1};
        vertices.push_back(vtx);
    };

    // Riding surface: extrude profile along X
    uint32_t surfBase = static_cast<uint32_t>(vertices.size());
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
        indices.insert(indices.end(), {a0, a1, b0, b0, a1, b1});
    }

    // Back wall at Z=0
    {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        glm::vec3 n = {0, 0, -1};
        pushVert({-hw, 0, 0}, n, 0, 0);
        pushVert({hw, 0, 0}, n, 1, 0);
        pushVert({hw, lip + h, 0}, n, 1, 1);
        pushVert({-hw, lip + h, 0}, n, 0, 1);
        indices.insert(indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }

    // Far wall (half-pipe only)
    if (p.bHalfPipe) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        glm::vec3 n = {0, 0, 1};
        pushVert({-hw, 0, totalDepth}, n, 0, 0);
        pushVert({hw, 0, totalDepth}, n, 1, 0);
        pushVert({hw, lip + h, totalDepth}, n, 1, 1);
        pushVert({-hw, lip + h, totalDepth}, n, 0, 1);
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    // Bottom
    {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        glm::vec3 n = {0, -1, 0};
        pushVert({-hw, 0, 0}, n, 0, 0);
        pushVert({hw, 0, 0}, n, 1, 0);
        pushVert({hw, 0, totalDepth}, n, 1, 1);
        pushVert({-hw, 0, totalDepth}, n, 0, 1);
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    // Side walls
    auto emitFan = [&](uint32_t base, int count, int side) {
        for (int i = 1; i < count - 1; i++) {
            if (side == 0)
                indices.insert(indices.end(), {base, base + (uint32_t)(i + 1), base + (uint32_t)i});
            else
                indices.insert(indices.end(), {base, base + (uint32_t)i, base + (uint32_t)(i + 1)});
        }
    };

    auto sideUV = [&](int i) -> std::pair<float, float> {
        return {profile[i].z / std::max(totalDepth, 0.01f), profile[i].y / std::max(h, 0.01f)};
    };

    // Profile split index: end of curve 1 + flat section (where Y=0 before curve 2)
    const int splitIdx = (h > R ? 2 : 0) + (seg + 1) + (fl > 0.0f ? 1 : 0);

    for (int side = 0; side < 2; side++) {
        const float x = side == 0 ? -hw : hw;
        const glm::vec3 n = side == 0 ? glm::vec3{-1, 0, 0} : glm::vec3{1, 0, 0};

        if (!p.bHalfPipe) {
            uint32_t base = static_cast<uint32_t>(vertices.size());
            pushVert({x, 0, 0}, n, 0, 0);
            pushVert({x, lip + h, 0}, n, 0, 1);
            for (int i = 0; i < profCount; i++) {
                auto [u, v] = sideUV(i);
                pushVert({x, profile[i].y, profile[i].z}, n, u, v);
            }
            pushVert({x, 0, totalDepth}, n, 1, 0);
            emitFan(base, 2 + profCount + 1, side);
        } else {
            // Near fan: bottom-near corner, wall 1, curve 1, flat
            {
                uint32_t base = static_cast<uint32_t>(vertices.size());
                pushVert({x, 0, 0}, n, 0, 0);
                pushVert({x, lip + h, 0}, n, 0, 1);
                for (int i = 0; i < splitIdx; i++) {
                    auto [u, v] = sideUV(i);
                    pushVert({x, profile[i].y, profile[i].z}, n, u, v);
                }
                emitFan(base, 2 + splitIdx, side);
            }
            // Far fan: bottom-far corner, curve 2, wall 2
            {
                uint32_t base = static_cast<uint32_t>(vertices.size());
                pushVert({x, 0, totalDepth}, n, 1, 0);
                for (int i = splitIdx; i < profCount; i++) {
                    auto [u, v] = sideUV(i);
                    pushVert({x, profile[i].y, profile[i].z}, n, u, v);
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
    std::vector<float> profR, profY;
    std::vector<glm::vec2> profN; // (radial inward, Y) components

    // Vertical wall above the curve (if h > cR)
    if (h > cR) {
        profR.push_back(flatR + cR);  profY.push_back(lip + h);   profN.push_back({-1.0f, 0.0f});
        profR.push_back(flatR + cR);  profY.push_back(lip + cR);  profN.push_back({-1.0f, 0.0f});
    }

    // Quarter-circle from wall down to floor
    for (int i = 0; i <= seg; i++) {
        const float t = static_cast<float>(i) / seg;
        const float angle = -t * (pi * 0.5f); // 0 to -PI/2
        const float r = flatR + cR * cosf(angle);
        const float y = lip + cR + cR * sinf(angle);
        profR.push_back(r);
        profY.push_back(y);
        profN.push_back({-cosf(angle), -sinf(angle)});
    }

    // Flat floor (if flatR > 0)
    if (flatR > 0.0f) {
        profR.push_back(0.0f);
        profY.push_back(lip);
        profN.push_back({0.0f, 1.0f});
    }

    const int profCount = static_cast<int>(profR.size());

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Revolve profile around Y axis
    for (int j = 0; j <= N; j++) {
        const float theta = static_cast<float>(j) / N * 2.0f * pi;
        const float cx = cosf(theta), cz = sinf(theta);
        for (int i = 0; i < profCount; i++) {
            Vertex v{};
            v.position = {cx * profR[i], profY[i], cz * profR[i]};
            v.normal = {cx * profN[i].x, profN[i].y, cz * profN[i].x};
            v.texcoordU = static_cast<float>(j) / N;
            v.texcoordV = static_cast<float>(i) / (profCount - 1);
            v.tangent = {-cz, 0, cx, 1.0f};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
    }

    for (int j = 0; j < N; j++) {
        for (int i = 0; i < profCount - 1; i++) {
            uint32_t a0 = j * profCount + i;
            uint32_t a1 = j * profCount + i + 1;
            uint32_t b0 = (j + 1) * profCount + i;
            uint32_t b1 = (j + 1) * profCount + i + 1;
            indices.insert(indices.end(), {a0, a1, b0, b0, a1, b1});
        }
    }

    // Bottom face at Y=0 (pivot point)
    {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        Vertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.texcoordU = 0.5f;
        cv.texcoordV = 0.5f;
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.push_back(cv);

        const float rimR = flatR + cR;
        for (int j = 0; j < N; j++) {
            const float theta = static_cast<float>(j) / N * 2.0f * pi;
            Vertex v{};
            v.position = {cosf(theta) * rimR, 0.0f, sinf(theta) * rimR};
            v.normal = {0, -1, 0};
            v.texcoordU = cosf(theta) * 0.5f + 0.5f;
            v.texcoordV = sinf(theta) * 0.5f + 0.5f;
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            vertices.push_back(v);
        }
        for (int j = 0; j < N; j++)
            indices.insert(indices.end(), {base, base + 1 + (uint32_t)j, base + 1 + (uint32_t)((j + 1) % N)});
    }

    // Outer wall (vertical cylinder at R = flatR + cR, from Y=0 to Y=lip+h)
    {
        const float wallR = flatR + cR;
        uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int j = 0; j <= N; j++) {
            const float theta = static_cast<float>(j) / N * 2.0f * pi;
            const float cx = cosf(theta), cz = sinf(theta);
            for (int k = 0; k < 2; k++) {
                Vertex v{};
                v.position = {cx * wallR, k == 0 ? 0.0f : lip + h, cz * wallR};
                v.normal = {cx, 0, cz};
                v.texcoordU = static_cast<float>(j) / N;
                v.texcoordV = static_cast<float>(k);
                v.tangent = {-cz, 0, cx, 1.0f};
                v.color = {1, 1, 1, 1};
                vertices.push_back(v);
            }
        }
        for (int j = 0; j < N; j++) {
            uint32_t a0 = base + j * 2, a1 = base + j * 2 + 1;
            uint32_t b0 = base + (j + 1) * 2, b1 = base + (j + 1) * 2 + 1;
            indices.insert(indices.end(), {a0, a1, b0, a1, b1, b0});
        }
    }

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateSpline(const Engine::SplineParams& p)
{
    const int N = static_cast<int>(p.controlPoints.Size());
    if (N < 2) return false;

    const int segs = std::max(1, p.segmentsPerSpan);
    const int sides = std::max(3, p.sides);
    const int totalSpans = N - 1;
    const int totalRings = totalSpans * segs + 1;

    auto getCP = [&](int i) -> glm::vec3 {
        if (i < 0) return 2.0f * glm::vec3(p.controlPoints[0]) - glm::vec3(p.controlPoints[1]);
        if (i >= N) return 2.0f * glm::vec3(p.controlPoints[N - 1]) - glm::vec3(p.controlPoints[N - 2]);
        return {p.controlPoints[i]};
    };

    auto catmullPos = [](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) -> glm::vec3 {
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * (t * t) + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * (t * t * t));
    };
    auto catmullTan = [](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) -> glm::vec3 {
        return 0.5f * ((-p0 + p2) + 2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t + 3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * (t * t));
    };
    auto catmullScalar = [](float v0, float v1, float v2, float v3, float t) -> float {
        return 0.5f * ((2.0f * v1) + (-v0 + v2) * t + (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * (t * t) + (-v0 + 3.0f * v1 - 3.0f * v2 + v3) * (t * t * t));
    };

    auto getRoll = [&](int i) -> float {
        if (i < 0) return p.controlPoints[0].w;
        if (i >= N) return p.controlPoints[N - 1].w;
        return p.controlPoints[i].w;
    };

    struct Frame
    {
        glm::vec3 pos;
        glm::vec3 tangent;
        glm::vec3 right;
        glm::vec3 up;
        glm::vec3 rRight;
        glm::vec3 rUp;
    };
    std::vector<Frame> frames;
    std::vector<float> perRingRoll;
    frames.reserve(totalRings);
    perRingRoll.reserve(totalRings);

    for (int i = 0; i < totalRings; i++) {
        int span;
        float t;
        if (i == totalRings - 1) {
            span = N - 2;
            t = 1.0f;
        }
        else {
            span = i / segs;
            t = static_cast<float>(i % segs) / static_cast<float>(segs);
        }

        glm::vec3 tang = catmullTan(getCP(span - 1), getCP(span), getCP(span + 1), getCP(span + 2), t);
        if (glm::length(tang) < 1e-6f) tang = frames.empty() ? glm::vec3{0, 0, 1} : frames.back().tangent;
        perRingRoll.push_back(catmullScalar(getRoll(span - 1), getRoll(span), getRoll(span + 1), getRoll(span + 2), t));
        frames.push_back({catmullPos(getCP(span - 1), getCP(span), getCP(span + 1), getCP(span + 2), t), glm::normalize(tang), {}, {}, {}, {}});
    }

    // Parallel transport frames
    {
        glm::vec3 worldUp = {0, 1, 0};
        if (glm::abs(glm::dot(frames[0].tangent, worldUp)) > 0.99f) worldUp = {1, 0, 0};
        frames[0].right = glm::normalize(glm::cross(worldUp, frames[0].tangent));
        frames[0].up = glm::normalize(glm::cross(frames[0].tangent, frames[0].right));
        for (int i = 1; i < totalRings; i++) {
            glm::vec3 axis = glm::cross(frames[i - 1].tangent, frames[i].tangent);
            float axisLen = glm::length(axis);
            if (axisLen < 1e-6f) {
                frames[i].right = frames[i - 1].right;
            }
            else {
                float cosA = glm::clamp(glm::dot(frames[i - 1].tangent, frames[i].tangent), -1.0f, 1.0f);
                glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::acos(cosA), axis / axisLen);
                frames[i].right = glm::normalize(glm::vec3(rot * glm::vec4(frames[i - 1].right, 0.0f)));
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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const int railCount = p.bDualPath ? 2 : 1;
    const float halfSpacing = p.dualPathSpacing * 0.5f;

    for (int rail = 0; rail < railCount; rail++) {
        const float offsetSign = (railCount == 1) ? 0.0f : (rail == 0 ? -1.0f : 1.0f);
        const auto baseVertex = static_cast<uint32_t>(vertices.size());

        for (int i = 0; i < totalRings; i++) {
            const Frame& f = frames[i];
            const glm::vec3 center = f.pos + offsetSign * halfSpacing * f.rRight;
            const float vCoord = (totalRings > 1) ? static_cast<float>(i) / static_cast<float>(totalRings - 1) : 0.0f;
            for (int j = 0; j < sides; j++) {
                const float angle = glm::two_pi<float>() * static_cast<float>(j) / sides;
                const glm::vec3 radial = glm::cos(angle) * f.rRight + glm::sin(angle) * f.rUp;
                Vertex v{};
                v.position = center + p.radius * radial;
                v.normal = radial;
                v.texcoordU = static_cast<float>(j) / static_cast<float>(sides);
                v.texcoordV = vCoord;
                v.tangent = {f.rRight.x, f.rRight.y, f.rRight.z, 1.0f};
                v.color = {1, 1, 1, 1};
                vertices.push_back(v);
            }
        }

        for (int i = 0; i < totalRings - 1; i++) {
            for (int j = 0; j < sides; j++) {
                uint32_t a0 = baseVertex + i * sides + j, a1 = baseVertex + i * sides + (j + 1) % sides;
                uint32_t b0 = baseVertex + (i + 1) * sides + j, b1 = baseVertex + (i + 1) * sides + (j + 1) % sides;
                indices.push_back(a0);
                indices.push_back(a1);
                indices.push_back(b0);
                indices.push_back(a1);
                indices.push_back(b1);
                indices.push_back(b0);
            }
        }

        if (p.bCaps) {
            {
                auto cIdx = static_cast<uint32_t>(vertices.size());
                const Frame& f = frames[0];
                const glm::vec3 center = f.pos + offsetSign * halfSpacing * f.rRight;
                Vertex vc{};
                vc.position = center;
                vc.normal = -f.tangent;
                vc.texcoordU = 0.5f;
                vc.texcoordV = 0.0f;
                vc.tangent = {f.right.x, f.right.y, f.right.z, 1.0f};
                vc.color = {1, 1, 1, 1};
                vertices.push_back(vc);
                for (int j = 0; j < sides; j++) {
                    indices.push_back(cIdx);
                    indices.push_back(baseVertex + (j + 1) % sides);
                    indices.push_back(baseVertex + j);
                }
            } {
                auto cIdx = static_cast<uint32_t>(vertices.size());
                const Frame& f = frames[totalRings - 1];
                uint32_t rStart = baseVertex + static_cast<uint32_t>((totalRings - 1) * sides);
                const glm::vec3 center = f.pos + offsetSign * halfSpacing * f.rRight;
                Vertex vc{};
                vc.position = center;
                vc.normal = f.tangent;
                vc.texcoordU = 0.5f;
                vc.texcoordV = 1.0f;
                vc.tangent = {f.right.x, f.right.y, f.right.z, 1.0f};
                vc.color = {1, 1, 1, 1};
                vertices.push_back(vc);
                for (int j = 0; j < sides; j++) {
                    indices.push_back(cIdx);
                    indices.push_back(rStart + j);
                    indices.push_back(rStart + (j + 1) % sides);
                }
            }
        }
    }

    if (p.bDualPath && p.bCrossPlanks) {
        constexpr float plankThickness = 0.1f;
        const int plankInterval = std::max(1, p.crossPlankInterval);

        auto makeVert = [](glm::vec3 pos, glm::vec3 normal, glm::vec3 tan, float u, float v) {
            Vertex vert{};
            vert.position = pos;
            vert.normal = normal;
            vert.texcoordU = u;
            vert.texcoordV = v;
            vert.tangent = {tan.x, tan.y, tan.z, 1.0f};
            vert.color = {1, 1, 1, 1};
            return vert;
        };

        for (int i = 0; i < totalRings - 1; i += plankInterval) {
            const Frame& f0 = frames[i];
            const Frame& f1 = frames[i + 1];

            // 8 corners of the plank box
            const float plankTop = p.radius + p.crossPlankHeight;
            const float plankBot = plankTop - plankThickness;
            const glm::vec3 tl0 = f0.pos - halfSpacing * f0.rRight + plankTop * f0.rUp;
            const glm::vec3 tr0 = f0.pos + halfSpacing * f0.rRight + plankTop * f0.rUp;
            const glm::vec3 bl0 = f0.pos - halfSpacing * f0.rRight + plankBot * f0.rUp;
            const glm::vec3 br0 = f0.pos + halfSpacing * f0.rRight + plankBot * f0.rUp;
            const glm::vec3 tl1 = f1.pos - halfSpacing * f1.rRight + plankTop * f1.rUp;
            const glm::vec3 tr1 = f1.pos + halfSpacing * f1.rRight + plankTop * f1.rUp;
            const glm::vec3 bl1 = f1.pos - halfSpacing * f1.rRight + plankBot * f1.rUp;
            const glm::vec3 br1 = f1.pos + halfSpacing * f1.rRight + plankBot * f1.rUp;

            auto base = static_cast<uint32_t>(vertices.size());

            // Top face (normal = rUp)
            vertices.push_back(makeVert(tl0, f0.rUp, f0.rRight, 0, 0));
            vertices.push_back(makeVert(tr0, f0.rUp, f0.rRight, 1, 0));
            vertices.push_back(makeVert(tl1, f1.rUp, f1.rRight, 0, 1));
            vertices.push_back(makeVert(tr1, f1.rUp, f1.rRight, 1, 1));
            // Bottom face (normal = -rUp)
            vertices.push_back(makeVert(bl0, -f0.rUp, f0.rRight, 0, 0));
            vertices.push_back(makeVert(br0, -f0.rUp, f0.rRight, 1, 0));
            vertices.push_back(makeVert(bl1, -f1.rUp, f1.rRight, 0, 1));
            vertices.push_back(makeVert(br1, -f1.rUp, f1.rRight, 1, 1));
            // Front face (normal = -tangent)
            vertices.push_back(makeVert(tl0, -f0.tangent, f0.rRight, 0, 1));
            vertices.push_back(makeVert(tr0, -f0.tangent, f0.rRight, 1, 1));
            vertices.push_back(makeVert(bl0, -f0.tangent, f0.rRight, 0, 0));
            vertices.push_back(makeVert(br0, -f0.tangent, f0.rRight, 1, 0));
            // Back face (normal = +tangent)
            vertices.push_back(makeVert(tl1, f1.tangent, f1.rRight, 0, 1));
            vertices.push_back(makeVert(tr1, f1.tangent, f1.rRight, 1, 1));
            vertices.push_back(makeVert(bl1, f1.tangent, f1.rRight, 0, 0));
            vertices.push_back(makeVert(br1, f1.tangent, f1.rRight, 1, 0));
            // Left face (normal = -rRight)
            vertices.push_back(makeVert(tl0, -f0.rRight, f0.tangent, 0, 1));
            vertices.push_back(makeVert(tl1, -f1.rRight, f1.tangent, 1, 1));
            vertices.push_back(makeVert(bl0, -f0.rRight, f0.tangent, 0, 0));
            vertices.push_back(makeVert(bl1, -f1.rRight, f1.tangent, 1, 0));
            // Right face (normal = +rRight)
            vertices.push_back(makeVert(tr0, f0.rRight, f0.tangent, 0, 1));
            vertices.push_back(makeVert(tr1, f1.rRight, f1.tangent, 1, 1));
            vertices.push_back(makeVert(br0, f0.rRight, f0.tangent, 0, 0));
            vertices.push_back(makeVert(br1, f1.rRight, f1.tangent, 1, 0));

            // 6 faces × 2 triangles each
            // Faces 0 (top), 3 (back), 4 (left) need flipped winding
            for (int face = 0; face < 6; face++) {
                uint32_t b = base + face * 4;
                if (face == 0 || face == 3 || face == 4) {
                    indices.push_back(b);
                    indices.push_back(b + 2);
                    indices.push_back(b + 1);
                    indices.push_back(b + 1);
                    indices.push_back(b + 2);
                    indices.push_back(b + 3);
                }
                else {
                    indices.push_back(b);
                    indices.push_back(b + 1);
                    indices.push_back(b + 2);
                    indices.push_back(b + 1);
                    indices.push_back(b + 3);
                    indices.push_back(b + 2);
                }
            }
        }
    }

    return FinalizeGeometry(vertices, indices);
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
