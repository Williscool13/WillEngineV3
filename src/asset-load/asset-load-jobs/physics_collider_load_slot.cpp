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

    // todo: procedural / imported model / text3D collider generation.
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
