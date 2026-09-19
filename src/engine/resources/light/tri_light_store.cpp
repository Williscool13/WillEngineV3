//
// Created by William on 2026-08-08.
//

#include "engine/resources/light/tri_light_store.h"

#include "engine/logging/engine_log.h"
#include "engine/resources/model/static_model.h"
#include "render/interface/render_interface.h"

namespace Engine
{
static_assert(TriLightStore::REUSE_DELAY_FRAMES >= Core::FRAME_BUFFER_COUNT, "A dead write must reach every host slot before the reservation is reused");

void TriLightStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag)
{
    ranges_.Init(capacity, alloc, tag, "TriLightStore");
    meshes_.Init(MAX_EMISSIVE_MESHES, alloc, tag, "TriLightMeshes");
    meshlets_.Init(MAX_EMISSIVE_MESHLETS, alloc, tag, "TriLightMeshlets");
    reservations_ = Core::HeapArray<Reservation>(alloc, tag, MAX_EMISSIVE_MESHES);
    dirty_.Init(MAX_EMISSIVE_MESHES, Core::FRAME_BUFFER_COUNT, alloc, tag);
    pendingFrees_ = Core::Vector<PendingFree>(alloc, tag);
}

uint32_t TriLightStore::Reserve(uint32_t instanceSlot, uint32_t triangleCount, uint32_t meshletCount, const char* ownerName)
{
    if (triangleCount == 0) { return INVALID_MESH_SLOT; }

    const Range meshSlot = meshes_.Allocate(1);
    if (!meshSlot.IsValid()) {
        if (!bWarnedCapReached_) {
            bWarnedCapReached_ = true;
            LOG_WARN(Engine, "Emissive instance cap ({}) reached; further emissive primitives will not light, starting with model ({})", MAX_EMISSIVE_MESHES, ownerName);
        }
        return INVALID_MESH_SLOT;
    }

    Range meshlets = meshlets_.Allocate(glm::max(meshletCount, 1u));
    if (!meshlets.IsValid()) {
        if (!bWarnedMeshletsFull_) {
            bWarnedMeshletsFull_ = true;
            LOG_WARN(Engine, "Emissive meshlet cap ({}) reached; model ({}) binned as one meshlet", MAX_EMISSIVE_MESHLETS, ownerName);
        }
        meshlets = meshlets_.Allocate(1);
    }
    if (!meshlets.IsValid()) {
        meshes_.Free(meshSlot);
        return INVALID_MESH_SLOT;
    }

    const Range range = ranges_.Allocate(triangleCount);
    if (!range.IsValid()) {
        meshlets_.Free(meshlets);
        meshes_.Free(meshSlot);
        if (!bWarnedFull_) {
            bWarnedFull_ = true;
            LOG_WARN(Engine, "Tri light store full; further emissive primitives get no triangle lights");
        }
        return INVALID_MESH_SLOT;
    }

    reservations_[meshSlot.offset] = {instanceSlot, range, meshlets, true};
    reservationCount_++;
    dirty_.Mark(meshSlot.offset);
    return meshSlot.offset;
}

void TriLightStore::Release(uint32_t meshSlot)
{
    Reservation& reservation = reservations_[meshSlot];
    if (!reservation.bLive) { return; }
    reservation.bLive = false;
    reservationCount_--;
    dirty_.Mark(meshSlot);
    pendingFrees_.PushBack({meshSlot, frame_});
    bWarnedCapReached_ = false;
}

void TriLightStore::SetEnabled(bool bEnabled)
{
    if (bEnabled == bEnabled_) { return; }
    bEnabled_ = bEnabled;
    if (bEnabled) { MarkAllDirty(); }
}

void TriLightStore::Tick(uint64_t frame)
{
    frame_ = frame;
    while (!pendingFrees_.IsEmpty() && frame_ - pendingFrees_[0].frame >= REUSE_DELAY_FRAMES) {
        const uint32_t meshSlot = pendingFrees_[0].meshSlot;
        ranges_.Free(reservations_[meshSlot].range);
        meshlets_.Free(reservations_[meshSlot].meshlets);
        meshes_.Free({meshSlot, 1});
        reservations_[meshSlot] = {};
        pendingFrees_.RemoveAt(0);
        bWarnedFull_ = false;
        bWarnedMeshletsFull_ = false;
    }
}
} // Engine
