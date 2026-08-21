//
// Created by William on 2026-07-03.
//

#include "physics_collider_load_slot.h"

#include <tracy/Tracy.hpp>

#include "meshoptimizer/src/meshoptimizer.h"

#include "asset-load/asset_load_types.h"
#include "asset-load/asset_load_utils.h"
#include "core/containers/heap_array.h"
#include "core/containers/span.h"
#include "core/containers/vector.h"
#include "core/memory/memory_manager.h"
#include "engine/compression/compression.h"
#include "engine/resources/model/model_format.h"
#include "engine/resources/physics/collider_generation.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "engine/spline/spline_frames.h"
#include "platform/file_utils.h"
#include "procedural_geometry.h"
#include "text3d_geometry.h"

namespace AssetLoad
{
/** meshopt_simplify a triangle mesh down toward a physics-friendly triangle budget, then re-index; below the threshold the mesh is copied as-is. Appends to the out vectors. */
static void SimplifyColliderMesh(Core::Span<const Vec3> positions, Core::Span<const uint32_t> indices, Core::MemoryManager* mm, Core::Vector<Vec3>& outPositions, Core::Vector<uint32_t>& outIndices)
{
    constexpr size_t kSimplifyThreshold = 1500;
    constexpr size_t kSimplifyFloor = 1500;
    constexpr float kSimplifyRatio = 0.15f;
    constexpr float kSimplifyError = 0.01f;
    if (indices.Size() > kSimplifyThreshold) {
        const size_t target = std::max(kSimplifyFloor, static_cast<size_t>(indices.Size() * kSimplifyRatio));
        Core::HeapArray<uint32_t> simplified(&mm->AssetsScratch(), Core::AllocTag::Physics, indices.Size());
        const size_t simplifiedCount = meshopt_simplify(
            simplified.Data(), indices.Data(), indices.Size(),
            &positions[0].x, positions.Size(), sizeof(Vec3),
            target, kSimplifyError);

        Core::HeapArray<uint32_t> remap(&mm->AssetsScratch(), Core::AllocTag::Physics, positions.Size());
        const size_t uniqueVerts = meshopt_generateVertexRemap(remap.Data(), simplified.Data(), simplifiedCount, positions.Data(), positions.Size(), sizeof(Vec3));

        Core::HeapArray<Vec3> remappedPositions(&mm->AssetsScratch(), Core::AllocTag::Physics, uniqueVerts);
        meshopt_remapVertexBuffer(remappedPositions.Data(), positions.Data(), positions.Size(), sizeof(Vec3), remap.Data());
        Core::HeapArray<uint32_t> remappedIndices(&mm->AssetsScratch(), Core::AllocTag::Physics, simplifiedCount);
        meshopt_remapIndexBuffer(remappedIndices.Data(), simplified.Data(), simplifiedCount, remap.Data());

        for (size_t i = 0; i < remappedPositions.Size(); ++i) { outPositions.PushBack(remappedPositions[i]); }
        for (size_t i = 0; i < remappedIndices.Size(); ++i) { outIndices.PushBack(remappedIndices[i]); }
    }
    else {
        for (size_t i = 0; i < positions.Size(); ++i) { outPositions.PushBack(positions[i]); }
        for (size_t i = 0; i < indices.Size(); ++i) { outIndices.PushBack(indices[i]); }
    }
}

/**
 * Reads a .wsmesh from disk (CPU-only: no GPU upload, no BLAS) and produces a simplified physics geometry: full-fidelity positions dequantized per primitive, then meshopt_simplify. ConvexHull consumers use positions only; TriangleMesh uses positions+indices.
 */
static bool ReadModelColliderGeometry(const Core::Path& source, Core::MemoryManager* mm, Core::Vector<Vec3>& outPositions, Core::Vector<uint32_t>& outIndices)
{
    ZoneScopedN("ReadModelColliderGeometry");

    if (!source.Exists()) { return false; }

    Engine::WStaticModelHeader header{};
    Core::HeapArray<Engine::Vertex> vertices{};
    Core::HeapArray<uint32_t> indices{};
    Core::HeapArray<Primitive> primitives{};

    {
        Platform::ScopedFileMapping map(source, true);
        if (!map.data) { return false; }
        auto optHeader = Engine::ReadWStaticModelHeader(map.data, map.size);
        if (!optHeader) { return false; }
        header = *optHeader;

        if (header.dataOffset + header.compressedBodySize > map.size) { return false; }

        Core::HeapArray<uint8_t> body(&mm->AssetsScratch(), Core::AllocTag::Physics, header.uncompressedBodySize);
        Engine::Decompress(header.compressionType, map.data + header.dataOffset, header.compressedBodySize, body.Data(), header.uncompressedBodySize);

        auto readArray = [&]<typename T>(Core::HeapArray<T>& vec, uint32_t offset, uint32_t count) {
            if (count > 0) {
                vec = Core::HeapArray<T>(&mm->AssetsScratch(), Core::AllocTag::Physics, count);
                memcpy(vec.Data(), body.Data() + offset, count * sizeof(T));
            }
        };
        readArray(vertices, header.vertexOffset, header.vertexCount);
        readArray(indices, header.indexOffset, header.indexCount);
        readArray(primitives, header.primitiveOffset, header.primitiveCount);
    }

    if (vertices.IsEmpty() || indices.IsEmpty() || primitives.IsEmpty()) { return false; }

    Core::HeapArray<Vec3> allPositions(&mm->AssetsScratch(), Core::AllocTag::Physics, vertices.Size());
    for (size_t pi = 0; pi < primitives.Size(); ++pi) {
        const Primitive& prim = primitives[pi];
        const Vec3 boundsMin = prim.boundingBoxMin;
        const Vec3 boundsExtents = prim.boundingBoxMax - prim.boundingBoxMin;

        const size_t indexStart = prim.indexOffset;
        const size_t indexEnd = pi + 1 < primitives.Size() ? primitives[pi + 1].indexOffset : indices.Size();

        uint32_t vertMin = UINT32_MAX, vertMax = 0;
        for (size_t ii = indexStart; ii < indexEnd; ++ii) {
            vertMin = std::min(vertMin, indices[ii]);
            vertMax = std::max(vertMax, indices[ii]);
        }
        for (uint32_t vi = vertMin; vi <= vertMax; ++vi) {
            allPositions[vi] = AssetLoad::DequantizeVertexPosition(vertices[vi], boundsMin, boundsExtents);
        }
    }

    SimplifyColliderMesh(Core::Span<const Vec3>(allPositions.Data(), allPositions.Size()), Core::Span<const uint32_t>(indices.Data(), indices.Size()), mm, outPositions, outIndices);
    return true;
}

PhysicsColliderLoadSlot::PhysicsColliderLoadSlot() = default;

PhysicsColliderLoadSlot::~PhysicsColliderLoadSlot() = default;

void PhysicsColliderLoadSlot::Initialize(enki::TaskScheduler* _scheduler, Core::MemoryManager* _memoryManager,
                                         Core::InlineFunction<void(bool, PhysicsColliderSlotHandle)> _notifyCallback)
{
    scheduler = _scheduler;
    memoryManager = _memoryManager;
    notifyCallback = std::move(_notifyCallback);
}

void PhysicsColliderLoadSlot::Launch(PhysicsColliderSlotHandle _slotHandle, Engine::PhysicsColliderAsset* _collider)
{
    slotHandle = _slotHandle;
    collider = _collider;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void PhysicsColliderLoadSlot::Clear()
{
    slotHandle = PhysicsColliderSlotHandle::INVALID;
    collider = nullptr;
}

bool PhysicsColliderLoadSlot::Build()
{
    ZoneScopedN("BuildPhysicsCollider");

    Engine::PhysicsColliderAsset* c = collider;

    if (c->splineParams != nullptr) {
        const Engine::SplineParams& sp = *c->splineParams;

        Core::Vector<Engine::SplineFrame> frames(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        if (!Engine::SampleSplineFrames(sp.spline, sp.segmentsPerSpan, sp.rollAngle, frames)) { return false; }

        Core::Vector<Engine::SplineColliderPrimitive> prims(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        Engine::BuildSplineColliderPrimitives(sp, Core::Span<const Engine::SplineFrame>(frames.Data(), frames.Size()), prims);
        if (prims.IsEmpty()) { return false; }

        c->kind = Engine::PhysicsColliderKind::Compound;
        c->primitives = Core::HeapArray<Engine::SplineColliderPrimitive>(&memoryManager->Assets(), Core::AllocTag::Physics, prims.Size());
        for (size_t i = 0; i < prims.Size(); ++i) { c->primitives[i] = prims[i]; }

        Core::HeapArray<Vec3> pts(&memoryManager->AssetsScratch(), Core::AllocTag::Physics, frames.Size());
        for (size_t i = 0; i < frames.Size(); ++i) { pts[i] = frames[i].pos; }
        c->bounds = ComputeBounds(Core::Span<Vec3>(pts.Data(), pts.Size()));
        return true;
    }

    if (c->proceduralParams.has_value()) {
        Core::Vector<Engine::SplineColliderPrimitive> prims(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        Core::Vector<Vec3> positions(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        Engine::PhysicsColliderKind kind{};
        if (!Engine::BuildProceduralCollider(c->proceduralParams.value(), kind, prims, positions)) {
            // Exotic type (Bowl / CurvedRamp / Klein / Trefoil)
            Core::Vector<Engine::FullVertex> genVerts(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            Core::Vector<uint32_t> genIndices(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            if (!GenerateNonAnalyticProceduralGeometry(c->proceduralParams.value(), memoryManager->AssetsScratch(), genVerts, genIndices)) { return false; }
            if (genVerts.IsEmpty() || genIndices.IsEmpty()) { return false; }

            Core::HeapArray<Vec3> genPositions(&memoryManager->AssetsScratch(), Core::AllocTag::Physics, genVerts.Size());
            for (size_t i = 0; i < genVerts.Size(); ++i) { genPositions[i] = genVerts[i].position; }

            Core::Vector<Vec3> simPositions(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            Core::Vector<uint32_t> simIndices(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            SimplifyColliderMesh(Core::Span<const Vec3>(genPositions.Data(), genPositions.Size()), Core::Span<const uint32_t>(genIndices.Data(), genIndices.Size()), memoryManager, simPositions, simIndices);
            if (simPositions.IsEmpty()) { return false; }

            c->positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::Physics, simPositions.Size());
            for (size_t i = 0; i < simPositions.Size(); ++i) { c->positions[i] = simPositions[i]; }

            if (c->kind == Engine::PhysicsColliderKind::TriangleMesh) {
                if (simIndices.IsEmpty()) { return false; }
                c->indices = Core::HeapArray<uint32_t>(&memoryManager->Assets(), Core::AllocTag::Physics, simIndices.Size());
                for (size_t i = 0; i < simIndices.Size(); ++i) { c->indices[i] = simIndices[i]; }
            }
            c->bounds = ComputeBounds(Core::Span<Vec3>(c->positions.Data(), c->positions.Size()));
            return true;
        }

        c->kind = kind;
        if (kind == Engine::PhysicsColliderKind::Compound) {
            if (prims.IsEmpty()) { return false; }
            c->primitives = Core::HeapArray<Engine::SplineColliderPrimitive>(&memoryManager->Assets(), Core::AllocTag::Physics, prims.Size());
            for (size_t i = 0; i < prims.Size(); ++i) { c->primitives[i] = prims[i]; }

            // Compound may carry ConvexHull children whose vertices live in the shared positions pool.
            if (!positions.IsEmpty()) {
                c->positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::Physics, positions.Size());
                for (size_t i = 0; i < positions.Size(); ++i) { c->positions[i] = positions[i]; }
            }

            Core::Vector<Vec3> boundsPts(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            for (const Engine::SplineColliderPrimitive& prim : prims) {
                if (prim.type == Engine::SplineColliderPrimitiveType::ConvexHull) { continue; } // verts folded in via the positions pool below
                Vec3 ext;
                switch (prim.type) {
                    case Engine::SplineColliderPrimitiveType::Capsule: ext = Vec3(prim.radius, prim.halfHeight + prim.radius, prim.radius); break;
                    case Engine::SplineColliderPrimitiveType::Sphere: ext = Vec3(prim.radius); break;
                    case Engine::SplineColliderPrimitiveType::Cylinder: ext = Vec3(prim.radius, prim.halfHeight, prim.radius); break;
                    default: ext = prim.halfExtents; break;
                }
                boundsPts.PushBack(prim.position - ext);
                boundsPts.PushBack(prim.position + ext);
            }
            for (size_t i = 0; i < positions.Size(); ++i) { boundsPts.PushBack(positions[i]); }
            c->bounds = ComputeBounds(Core::Span<Vec3>(boundsPts.Data(), boundsPts.Size()));
        }
        else {
            if (positions.IsEmpty()) { return false; }
            c->positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::Physics, positions.Size());
            for (size_t i = 0; i < positions.Size(); ++i) { c->positions[i] = positions[i]; }
            c->bounds = ComputeBounds(Core::Span<Vec3>(c->positions.Data(), c->positions.Size()));
        }
        return true;
    }

    if (c->sourceModelId.IsValid()) {
        Core::Vector<Vec3> positions(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        Core::Vector<uint32_t> indices(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        if (!ReadModelColliderGeometry(c->source, memoryManager, positions, indices)) { return false; }
        if (positions.IsEmpty()) { return false; }

        c->positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::Physics, positions.Size());
        for (size_t i = 0; i < positions.Size(); ++i) { c->positions[i] = positions[i]; }

        // ConvexHull ignores indices (Jolt hulls the point cloud); TriangleMesh needs them.
        if (c->kind == Engine::PhysicsColliderKind::TriangleMesh) {
            if (indices.IsEmpty()) { return false; }
            c->indices = Core::HeapArray<uint32_t>(&memoryManager->Assets(), Core::AllocTag::Physics, indices.Size());
            for (size_t i = 0; i < indices.Size(); ++i) { c->indices[i] = indices[i]; }
        }

        c->bounds = ComputeBounds(Core::Span<Vec3>(c->positions.Data(), c->positions.Size()));
        return true;
    }

    if (c->text3DParams != nullptr) {
        const Engine::Text3DParams& tp = *c->text3DParams;
        if (tp.font == nullptr) { return false; }

        if (c->bPreciseText3D) {
            Core::Vector<Engine::FullVertex> genVerts(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            Core::Vector<uint32_t> genIndices(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            if (!BuildText3DGeometry(*tp.font, tp, memoryManager->AssetsScratch(), genVerts, genIndices)) { return false; }
            if (genVerts.IsEmpty() || genIndices.IsEmpty()) { return false; }

            Core::HeapArray<Vec3> genPositions(&memoryManager->AssetsScratch(), Core::AllocTag::Physics, genVerts.Size());
            for (size_t i = 0; i < genVerts.Size(); ++i) { genPositions[i] = genVerts[i].position; }

            Core::Vector<Vec3> simPositions(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            Core::Vector<uint32_t> simIndices(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
            SimplifyColliderMesh(Core::Span<const Vec3>(genPositions.Data(), genPositions.Size()), Core::Span<const uint32_t>(genIndices.Data(), genIndices.Size()), memoryManager, simPositions, simIndices);
            if (simPositions.IsEmpty() || simIndices.IsEmpty()) { return false; }

            c->kind = Engine::PhysicsColliderKind::TriangleMesh;
            c->positions = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::Physics, simPositions.Size());
            for (size_t i = 0; i < simPositions.Size(); ++i) { c->positions[i] = simPositions[i]; }
            c->indices = Core::HeapArray<uint32_t>(&memoryManager->Assets(), Core::AllocTag::Physics, simIndices.Size());
            for (size_t i = 0; i < simIndices.Size(); ++i) { c->indices[i] = simIndices[i]; }
            c->bounds = ComputeBounds(Core::Span<Vec3>(c->positions.Data(), c->positions.Size()));
            return true;
        }

        Core::Vector<Engine::SplineColliderPrimitive> prims(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        if (!Engine::BuildText3DColliderPrimitives(*tp.font, tp, prims)) { return false; }
        if (prims.IsEmpty()) { return false; }

        c->kind = Engine::PhysicsColliderKind::Compound;
        c->primitives = Core::HeapArray<Engine::SplineColliderPrimitive>(&memoryManager->Assets(), Core::AllocTag::Physics, prims.Size());
        for (size_t i = 0; i < prims.Size(); ++i) { c->primitives[i] = prims[i]; }

        Core::Vector<Vec3> boundsPts(&memoryManager->AssetsScratch(), Core::AllocTag::Physics);
        for (const Engine::SplineColliderPrimitive& prim : prims) {
            boundsPts.PushBack(prim.position - prim.halfExtents);
            boundsPts.PushBack(prim.position + prim.halfExtents);
        }
        c->bounds = ComputeBounds(Core::Span<Vec3>(boundsPts.Data(), boundsPts.Size()));
        return true;
    }

    return false;
}

void PhysicsColliderLoadSlot::BuildColliderTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    const bool bSuccess = loadSlot->Build();
    if (loadSlot->notifyCallback) {
        loadSlot->notifyCallback(bSuccess, loadSlot->slotHandle);
    }
}
} // AssetLoad
