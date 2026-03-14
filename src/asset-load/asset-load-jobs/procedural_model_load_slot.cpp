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
            u = pos.x;
            v = pos.z;
        } // tread / bottom
        else if (absN.x >= absN.z) {
            u = pos.z;
            v = pos.y;
        } // cap faces
        else {
            u = pos.x;
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
        uint32_t base = static_cast<uint32_t>(vertices.size());
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

    // +X: normal=(1,0,0), U=Z, V=Y
    addFace({1, 0, 0}, {0, 0, 1},
            {sx, 0, 0}, {sx, sy, 0}, {sx, sy, sz}, {sx, 0, sz},
            {0, 0}, {0, sy}, {sz, sy}, {sz, 0});
    // -X: normal=(-1,0,0), U=Z(mirrored), V=Y
    addFace({-1, 0, 0}, {0, 0, -1},
            {0, 0, sz}, {0, sy, sz}, {0, sy, 0}, {0, 0, 0},
            {sz, 0}, {sz, sy}, {0, sy}, {0, 0});
    // +Y: normal=(0,1,0), U=X, V=Z
    addFace({0, 1, 0}, {1, 0, 0},
            {0, sy, 0}, {0, sy, sz}, {sx, sy, sz}, {sx, sy, 0},
            {0, 0}, {0, sz}, {sx, sz}, {sx, 0});
    // -Y: normal=(0,-1,0), U=X, V=Z
    addFace({0, -1, 0}, {1, 0, 0},
            {0, 0, 0}, {sx, 0, 0}, {sx, 0, sz}, {0, 0, sz},
            {0, 0}, {sx, 0}, {sx, sz}, {0, sz});
    // +Z: normal=(0,0,1), U=X, V=Y
    addFace({0, 0, 1}, {1, 0, 0},
            {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz},
            {0, 0}, {sx, 0}, {sx, sy}, {0, sy});
    // -Z: normal=(0,0,-1), U=X(mirrored), V=Y
    addFace({0, 0, -1}, {-1, 0, 0},
            {0, 0, 0}, {0, sy, 0}, {sx, sy, 0}, {sx, 0, 0},
            {0, 0}, {0, sy}, {sx, sy}, {sx, 0});

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
        uint32_t capBase = static_cast<uint32_t>(vertices.size());
        Vertex cv{};
        cv.position = {0, hh, 0};
        cv.normal = {0, 1, 0};
        cv.texcoordU = 0.5f;
        cv.texcoordV = 0.5f;
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.push_back(cv);
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / N * 2.0f * glm::pi<float>();
            Vertex v{};
            v.position = {cosf(angle) * r, hh, sinf(angle) * r};
            v.normal = {0, 1, 0};
            v.texcoordU = cosf(angle) * 0.5f + 0.5f;
            v.texcoordV = sinf(angle) * 0.5f + 0.5f;
            v.tangent = {1, 0, 0, 1};
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
            const float angle = static_cast<float>(j) / N * 2.0f * glm::pi<float>();
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

    const float pi = glm::pi<float>();
    const float pi2 = 2.0f * pi;

    // Helper: add a ring of (N+1) vertices at given y, ringR, outward normal y-component
    auto addRing = [&](float ringY, float ringR, float nY, float nXZScale) {
        for (int j = 0; j <= N; j++) {
            const float angle = static_cast<float>(j) / N * pi2;
            const float cx = cosf(angle), cz = sinf(angle);
            Vertex v{};
            v.position = {cx * ringR, ringY, cz * ringR};
            v.normal = {cx * nXZScale, nY, cz * nXZScale};
            v.texcoordU = static_cast<float>(j) / N;
            v.texcoordV = (ringY + bhh + r) / (2.0f * (bhh + r)); // 0=bottom, 1=top
            v.tangent = {-cz, 0, cx, 1.0f};
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
        const float phi = pi * 0.5f * static_cast<float>(i) / HR; // 0→pi/2
        topRingBase[i] = static_cast<uint32_t>(vertices.size());
        addRing(r * cosf(phi) + bhh, r * sinf(phi), cosf(phi), sinf(phi));
    }

    // Bottom hemisphere rings (ring 1 = equator, ring HR = near pole)
    std::vector<uint32_t> botRingBase(HR + 1);
    for (int i = 1; i <= HR; i++) {
        const float psi = pi * 0.5f * static_cast<float>(i) / HR; // 0→pi/2
        botRingBase[i] = static_cast<uint32_t>(vertices.size());
        addRing(-r * sinf(psi) - bhh, r * cosf(psi), -sinf(psi), cosf(psi));
    }

    // Bottom pole
    const uint32_t botPole = static_cast<uint32_t>(vertices.size()); {
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

    par_shapes_mesh* m = par_shapes_create_torus(slices, stacks, tr / rr);
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

    const int N = std::max(2, p.sides);
    const float outerR = p.width * 0.5f;
    const float innerR = std::max(0.01f, outerR - p.thickness);
    const float legH = std::max(0.0f, p.height - outerR);
    const float depth = p.depth;
    const float pi = glm::pi<float>();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Build outer and inner arc contour points (in XY, N+1 points each)
    // θ from 0→π: arc goes from right (+X) over top to left (-X)
    std::vector<glm::vec2> outerArc(N + 1), innerArc(N + 1);
    for (int i = 0; i <= N; i++) {
        const float theta = pi * static_cast<float>(i) / N;
        outerArc[i] = {outerR * cosf(theta), legH + outerR * sinf(theta)};
        innerArc[i] = {innerR * cosf(theta), legH + innerR * sinf(theta)};
    }

    // Outer contour (N+3 points: bottom-right, arc[0..N], bottom-left)
    // Inner contour same
    std::vector<glm::vec2> outerC, innerC;
    outerC.push_back({outerR, 0});
    for (auto& pt : outerArc) outerC.push_back(pt);
    outerC.push_back({-outerR, 0});

    innerC.push_back({innerR, 0});
    for (auto& pt : innerArc) innerC.push_back(pt);
    innerC.push_back({-innerR, 0});

    const int M = static_cast<int>(outerC.size()); // N+3

    // Helper: add a flat quad with given normal and basic UV
    auto addQuad = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 n) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        auto push = [&](glm::vec3 pos) {
            Vertex v{};
            v.position = pos;
            v.normal = n;
            v.texcoordU = pos.x;
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

    // Outer side walls: extrude each outer contour edge along Z
    // Segment 0 = right leg (outerC[0]→outerC[1]): normal (+1,0,0)
    // Segments 1..N = arc: normal outward from arc center (0, legH)
    // Segment N+1 = left leg: normal (-1,0,0)
    for (int i = 0; i < M - 1; i++) {
        glm::vec3 n;
        if (i == 0) {
            n = {1, 0, 0};
        }
        else if (i == M - 2) {
            n = {-1, 0, 0};
        }
        else {
            // Arc segment: normal points from arc center to midpoint
            const glm::vec2 mid = (outerC[i] + outerC[i + 1]) * 0.5f;
            const glm::vec2 dir = glm::normalize(mid - glm::vec2(0, legH));
            n = {dir.x, dir.y, 0};
        }
        addQuad(
            {outerC[i].x, outerC[i].y, 0},
            {outerC[i + 1].x, outerC[i + 1].y, 0},
            {outerC[i + 1].x, outerC[i + 1].y, depth},
            {outerC[i].x, outerC[i].y, depth},
            n);
    }

    // Inner side walls: reversed winding (faces toward door opening)
    for (int i = 0; i < M - 1; i++) {
        glm::vec3 n;
        if (i == 0) {
            n = {-1, 0, 0};
        }
        else if (i == M - 2) {
            n = {1, 0, 0};
        }
        else {
            const glm::vec2 mid = (innerC[i] + innerC[i + 1]) * 0.5f;
            const glm::vec2 dir = glm::normalize(glm::vec2(0, legH) - mid);
            n = {dir.x, dir.y, 0};
        }
        addQuad(
            {innerC[i + 1].x, innerC[i + 1].y, 0},
            {innerC[i].x, innerC[i].y, 0},
            {innerC[i].x, innerC[i].y, depth},
            {innerC[i + 1].x, innerC[i + 1].y, depth},
            n);
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
        uint32_t base = static_cast<uint32_t>(vertices.size());
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
        push(v0, uv0); push(v1, uv1); push(v2, uv2); push(v3, uv3);
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };

    auto addTri = [&](glm::vec3 n, glm::vec3 t,
                      glm::vec3 v0, glm::vec3 v1, glm::vec3 v2,
                      glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
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
        push(v0, uv0); push(v1, uv1); push(v2, uv2);
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
            {0, 0}, {0, slopeLen}, {sx, slopeLen}, {sx, 0});

    // Left cap (-X): (v1-v0)×(v2-v0) = (0,0,sz)×(0,sy,sz) → (-sz*sy,0,0) → -X ✓
    addTri({-1, 0, 0}, {0, 0, 1},
           {0, 0, 0}, {0, 0, sz}, {0, sy, sz},
           {0, 0}, {sz, 0}, {sz, sy});

    // Right cap (+X): (v1-v0)×(v2-v0) = (0,sy,sz)×(0,0,sz) → (sy*sz,0,0) → +X ✓
    addTri({1, 0, 0}, {0, 0, 1},
           {sx, 0, 0}, {sx, sy, sz}, {sx, 0, sz},
           {0, 0}, {sz, sy}, {sz, 0});

    return FinalizeGeometry(vertices, indices);
}

bool ProceduralModelLoadSlot::GenerateCone(const Engine::ConeParams& p)
{
    ZoneScopedN("GenerateCone");

    const int N = std::max(3, p.slices);
    const float r = p.radius;
    const float h = p.height;

    // par_shapes cone: Z-up, Z=0 base (radius=1), Z=1 apex (radius=0), no cap.
    // Unweld + compute_normals → flat normals (correct for pyramid at low slices).
    // Note: par_shapes_unweld does not copy tcoords, so UV is computed from position.
    par_shapes_mesh* m = par_shapes_create_cone(N, 1);
    if (!m) return false;

    par_shapes_unweld(m, true);
    par_shapes_compute_normals(m);

    // Remap to Y-up (proper rotation, winding preserved): engine = (par.x*r, par.z*h, -par.y*r)
    std::vector<Vertex> vertices(m->npoints);
    for (int i = 0; i < m->npoints; ++i) {
        const float px = m->points[i * 3 + 0], py = m->points[i * 3 + 1], pz = m->points[i * 3 + 2];
        const glm::vec3 pos = {px * r, pz * h, -py * r};
        vertices[i].position = pos;
        const float nx = m->normals[i * 3 + 0], ny = m->normals[i * 3 + 1], nz = m->normals[i * 3 + 2];
        vertices[i].normal = glm::normalize(glm::vec3{nx, nz, -ny});
        // UV from position: u=angular 0→1, v=height 0→1
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
        uint32_t capBase = static_cast<uint32_t>(vertices.size());
        Vertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.texcoordU = 0.5f;
        cv.texcoordV = 0.5f;
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        vertices.push_back(cv);
        for (int j = 0; j < N; j++) {
            const float angle = static_cast<float>(j) / N * 2.0f * glm::pi<float>();
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
