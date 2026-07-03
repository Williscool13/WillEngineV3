//
// Created by William on 2026-07-03.
//

#include "physics_collider_load_slot.h"

#include <tracy/Tracy.hpp>

#include "asset-load/asset_load_utils.h"
#include "core/containers/heap_array.h"
#include "core/containers/span.h"
#include "core/containers/vector.h"
#include "core/memory/memory_manager.h"
#include "engine/resources/physics/collider_generation.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "engine/spline/spline_frames.h"

namespace AssetLoad
{
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

    if (c->splineParams.has_value()) {
        const Engine::SplineParams& sp = c->splineParams.value();

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
        if (!Engine::BuildProceduralCollider(c->proceduralParams.value(), kind, prims, positions)) { return false; }

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

    // todo: imported model / text3D collider generation.
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
