//
// Created by William on 2026-07-01.
//

#include "engine/resources/model/instance_store.h"

#include "engine/material_manager.h"
#include "engine/resources/light/tri_light_store.h"
#include "engine/resources/model/static_model.h"
#include "render/shaders/lights_interop.h"
#include "engine/logging/engine_log.h"

namespace Engine
{
void InstanceStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::VirtualMemoryManager* vm, Core::AllocTag tag)
{
    instances_ = Core::VirtualArray<InstanceSource>(vm, tag, capacity, "InstanceStore");
    gpuInstances_ = Core::VirtualArray<Instance>(vm, tag, capacity, "InstanceStoreGPU");
    ranges_.Init(capacity, alloc, tag, "InstanceStore");
}

InstanceStore::Range InstanceStore::Allocate(uint32_t count)
{
    const Range range = ranges_.Allocate(count);
    if (range.IsValid()) {
        instances_.EnsureCommitted(ranges_.GetWatermark());
        gpuInstances_.EnsureCommitted(ranges_.GetWatermark());
        for (uint32_t i = 0; i < range.count; ++i) {
            gpuInstances_[range.offset + i] = DEAD_INSTANCE;
        }
    }
    return range;
}

void InstanceStore::Free(Range range)
{
    if (range.IsValid()) {
        for (uint32_t i = 0; i < range.count; ++i) {
            gpuInstances_[range.offset + i] = DEAD_INSTANCE;
        }
    }
    ranges_.Free(range);
    instances_.Trim(ranges_.GetWatermark());
    gpuInstances_.Trim(ranges_.GetWatermark());
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

void InstanceStore::FillEntry(uint32_t slot, MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, const PrimitiveProperty& primitive, const InstanceFill& fill)
{
    materialManager->AcquireMaterial(fill.material);
    const MaterialProperties props = materialManager->GetProperties(fill.material);
    const float maxEmissive = glm::max(props.emissiveFactor.x, glm::max(props.emissiveFactor.y, props.emissiveFactor.z));
    const bool bIsMaterialEmissive = props.emissiveFactor.w * maxEmissive > 0.0f;
    const uint32_t materialIndex = materialManager->GetMaterialIndex(fill.material);
    const TriLightStore::Range triLightRange = fill.bEmissiveLight && bIsMaterialEmissive && triLightStore ? triLightStore->Allocate(primitive.triangleCount) : TriLightStore::Range{};

    instances_[slot] = {
        .primitiveIndex = primitive.index,
        .sourceNodeIndex = fill.sourceNodeIndex,
        .modelPrimitiveOrdinal = fill.modelPrimitiveOrdinal,
        .modelSlot = fill.modelSlot,
        .materialSlot = fill.materialSlot,
        .materialIndex = materialIndex,
        .materialID = fill.material,
        .blasDeviceAddress = primitive.blasDeviceAddress,
        .modelSpaceTransform = fill.modelSpaceTransform,
        .triLightRange = triLightRange,
    };
    WriteRecord(slot);
}

void InstanceStore::WriteRecord(uint32_t slot)
{
    const InstanceSource& src = instances_[slot];
    if (!src.bVisible) {
        gpuInstances_[slot] = DEAD_INSTANCE;
        return;
    }
    gpuInstances_[slot] = {
        .primitiveIndex = src.primitiveIndex,
        .modelIndex = src.modelSlot,
        .materialIndex = src.materialIndex,
        .flags = src.flags,
        .stableId = src.stableId,
        .lightIndex = src.lightIndex,
        .emissiveTriLightBase = src.triLightRange.IsValid() ? static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + src.triLightRange.offset : ~0u,
        .blasDeviceAddress = src.blasDeviceAddress,
    };
}

void InstanceStore::SetMaterial(uint32_t slot, MaterialManager* materialManager, MaterialID material)
{
    instances_[slot].materialID = material;
    instances_[slot].materialIndex = materialManager->GetMaterialIndex(material);
    WriteRecord(slot);
}

void InstanceStore::SetLightIndex(uint32_t slot, uint32_t lightIndex)
{
    instances_[slot].lightIndex = lightIndex;
    WriteRecord(slot);
}

void InstanceStore::SetRenderState(Range range, bool bVisible, uint32_t flags, uint64_t stableId)
{
    for (uint32_t i = 0; i < range.count; ++i) {
        const uint32_t slot = range.offset + i;
        instances_[slot].bVisible = bVisible;
        instances_[slot].flags = flags;
        instances_[slot].stableId = stableId;
        WriteRecord(slot);
    }
}

void InstanceStore::ReleaseAndFree(MaterialManager* materialManager, TriLightStore* triLightStore, Range& range)
{
    if (!range.IsValid()) { return; }
    for (uint32_t i = 0; i < range.count; ++i) {
        materialManager->ReleaseMaterial(instances_[range.offset + i].materialID);
        triLightStore->Free(instances_[range.offset + i].triLightRange);
    }
    Free(range);
    range = {};
}
} // Engine
