//
// Created by William on 2026-07-01.
//

#include "engine/resources/model/instance_store.h"

#include <cstring>

#include "engine/material_manager.h"
#include "engine/resources/light/tri_light_store.h"
#include "engine/resources/model/static_model.h"
#include "render/shaders/lights_interop.h"
#include "render/interface/render_interface.h"
#include "engine/logging/engine_log.h"

namespace Engine
{
void InstanceStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::VirtualMemoryManager* vm, Core::AllocTag tag)
{
    records.sources = Core::VirtualArray<InstanceSource>(vm, tag, capacity, "InstanceStore");
    records.gpuInstances = Core::VirtualArray<Instance>(vm, tag, capacity, "InstanceStoreGPU");
    worldBounds.bounds = Core::VirtualArray<Vec4>(vm, tag, capacity, "InstanceStoreBounds");
    boundsChanges.changes = Core::Vector<Vec4>(alloc, tag);
    records.ranges.Init(capacity, alloc, tag, "InstanceStore");
    dirty.bits.Init(capacity, Core::FRAME_BUFFER_COUNT, alloc, tag);
}

InstanceStore::Range InstanceStore::Allocate(uint32_t count)
{
    const Range range = records.ranges.Allocate(count);
    if (range.IsValid()) {
        records.sources.EnsureCommitted(GetWatermark());
        records.gpuInstances.EnsureCommitted(GetWatermark());
        worldBounds.bounds.EnsureCommitted(GetWatermark());
        for (uint32_t i = 0; i < range.count; ++i) {
            records.gpuInstances[range.offset + i] = DEAD_INSTANCE;
            worldBounds.bounds[range.offset + i] = Vec4(0.0f);
            dirty.bits.Mark(range.offset + i);
        }
    }
    return range;
}

void InstanceStore::Free(Range range)
{
    if (range.IsValid()) {
        for (uint32_t i = 0; i < range.count; ++i) {
            worldBounds.PushChange(range.offset + i, boundsChanges);
            records.gpuInstances[range.offset + i] = DEAD_INSTANCE;
            worldBounds.bounds[range.offset + i] = Vec4(0.0f);
            dirty.bits.Mark(range.offset + i);
        }
    }
    records.ranges.Free(range);
    records.sources.Trim(GetWatermark());
    records.gpuInstances.Trim(GetWatermark());
    worldBounds.bounds.Trim(GetWatermark());
}

InstanceStore::Range InstanceStore::AllocateSingleMeshRange(MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, MaterialID material, uint32_t modelSlot, bool bEmissiveLight)
{
    if (model->modelData.meshes.IsEmpty()) { return {}; }
    MeshInformation& mesh = model->modelData.meshes[0];
    const auto count = static_cast<uint32_t>(mesh.primitiveProperties.Size());
    if (count == 0) { return {}; }

    Range range = Allocate(count);
    if (!range.IsValid()) {
        LOG_ERROR(Engine, "Instance store full; cannot allocate single-mesh range for model ({})", model->name.c_str());
        return {};
    }

    uint32_t writeIndex = range.offset;
    for (uint32_t j = 0; j < count; ++j) {
        FillEntry(writeIndex, materialManager, triLightStore, model, mesh.primitiveProperties[j], {
            .material = material,
            .modelSlot = modelSlot,
            .modelPrimitiveOrdinal = j,
            .bEmissiveLight = bEmissiveLight,
        });
        ++writeIndex;
    }
    return range;
}

void InstanceStore::ReleaseAndFree(MaterialManager* materialManager, TriLightStore* triLightStore, Range& range)
{
    if (!range.IsValid()) { return; }
    for (uint32_t i = 0; i < range.count; ++i) {
        InstanceSource& instance = records.sources[range.offset + i];
        materialManager->ReleaseMaterial(instance.materialID);
        if (instance.emissiveMeshSlot != TriLightStore::INVALID_MESH_SLOT) {
            triLightStore->Release(instance.emissiveMeshSlot);
            instance.emissiveMeshSlot = TriLightStore::INVALID_MESH_SLOT;
        }
    }
    Free(range);
    range = {};
}

void InstanceStore::FillEntry(uint32_t slot, MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, const PrimitiveProperty& primitive, const InstanceFill& fill)
{
    materialManager->AcquireMaterial(fill.material);
    const Material* material = materialManager->GetMaterial(fill.material);
    const MaterialProperties& props = material->props;
    const float maxEmissive = glm::max(props.emissiveFactor.x, glm::max(props.emissiveFactor.y, props.emissiveFactor.z));
    const bool bIsMaterialEmissive = props.emissiveFactor.w * maxEmissive > 0.0f;
    // Editable materials get a range even while black: nothing re-resolves the mesh when one lights up.
    const bool bMayEmitLater = !material->bSynthesized && !material->immutable;
    const uint32_t materialIndex = materialManager->GetMaterialIndex(fill.material);

    uint32_t emissiveMeshSlot = TriLightStore::INVALID_MESH_SLOT;
    if (fill.bEmissiveLight && (bIsMaterialEmissive || bMayEmitLater)) {
        emissiveMeshSlot = triLightStore->Reserve(slot, primitive.triangleCount, primitive.meshletCount, model->name.c_str());
    }

    records.sources[slot] = {
        .primitiveIndex = primitive.index,
        .sourceNodeIndex = fill.sourceNodeIndex,
        .modelPrimitiveOrdinal = fill.modelPrimitiveOrdinal,
        .modelSlot = fill.modelSlot,
        .materialSlot = fill.materialSlot,
        .materialIndex = materialIndex,
        .materialID = fill.material,
        .blasDeviceAddress = primitive.blasDeviceAddress,
        .modelSpaceTransform = fill.modelSpaceTransform,
        .localBounds = primitive.boundingSphere,
        .emissiveMeshSlot = emissiveMeshSlot,
    };
    WriteRecord(slot, triLightStore);
}

Instance InstanceStore::MakeRecord(uint32_t slot, const TriLightStore* triLightStore) const
{
    const InstanceSource& src = records.sources[slot];
    if (!src.bVisible) { return DEAD_INSTANCE; }
    return {
        .primitiveIndex = src.primitiveIndex,
        .modelIndex = src.modelSlot,
        .materialIndex = src.materialIndex,
        .flags = src.flags,
        .stableId = src.stableId,
        .lightIndex = src.lightIndex,
        .emissiveTriLightBase = src.emissiveMeshSlot != TriLightStore::INVALID_MESH_SLOT ? static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + triLightStore->Get(src.emissiveMeshSlot).range.offset : ~0u,
        .blasDeviceAddress = src.blasDeviceAddress,
    };
}

void InstanceStore::WriteRecord(uint32_t slot, TriLightStore* triLightStore)
{
    dirty.bits.Mark(slot);
    records.gpuInstances[slot] = MakeRecord(slot, triLightStore);
    if (records.sources[slot].emissiveMeshSlot != TriLightStore::INVALID_MESH_SLOT) { triLightStore->MarkDirty(records.sources[slot].emissiveMeshSlot); }
}

uint32_t InstanceStore::VerifyRecords(const TriLightStore* triLightStore) const
{
    uint32_t stale = 0;
    for (uint32_t slot = 0; slot < GetWatermark(); ++slot) {
        const Instance expected = MakeRecord(slot, triLightStore);
        if (memcmp(&expected, &records.gpuInstances[slot], sizeof(Instance)) != 0) { ++stale; }
    }
    return stale;
}

void InstanceStore::SetMaterial(uint32_t slot, MaterialManager* materialManager, TriLightStore* triLightStore, MaterialID material)
{
    worldBounds.PushChange(slot, boundsChanges);
    records.sources[slot].materialID = material;
    records.sources[slot].materialIndex = materialManager->GetMaterialIndex(material);
    WriteRecord(slot, triLightStore);
}

void InstanceStore::SetLightIndex(uint32_t slot, TriLightStore* triLightStore, uint32_t lightIndex)
{
    records.sources[slot].lightIndex = lightIndex;
    WriteRecord(slot, triLightStore);
}

void InstanceStore::SetRenderState(Range range, TriLightStore* triLightStore, bool bVisible, uint32_t flags, uint64_t stableId)
{
    for (uint32_t i = 0; i < range.count; ++i) {
        const uint32_t slot = range.offset + i;
        InstanceSource& source = records.sources[slot];
        if (source.bVisible != bVisible || source.flags != flags) {
            worldBounds.PushChange(slot, boundsChanges);
        }
        source.bVisible = bVisible;
        source.flags = flags;
        source.stableId = stableId;
        WriteRecord(slot, triLightStore);
    }
}
} // Engine
