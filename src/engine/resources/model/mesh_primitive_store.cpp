//
// Created by William on 2026-07-01.
//

#include "engine/resources/model/mesh_primitive_store.h"

#include "engine/material_manager.h"
#include "engine/resources/model/static_model.h"
#include "engine/logging/engine_log.h"

namespace Engine
{
void MeshPrimitiveStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag)
{
    instances_ = Core::HeapArray<MeshPrimitiveInstance>(alloc, tag, capacity);
    ranges_.Init(capacity, alloc, tag, "MeshPrimitiveStore");
}

MeshPrimitiveStore::Range MeshPrimitiveStore::AllocateSingleMeshRange(MaterialManager* materialManager, StaticModel* model, MaterialID material)
{
    if (model->modelData.meshes.IsEmpty()) { return {}; }
    MeshInformation& mesh = model->modelData.meshes[0];
    const auto count = static_cast<uint32_t>(mesh.primitiveProperties.Size());
    if (count == 0) { return {}; }

    Range range = ranges_.Allocate(count);
    if (!range.IsValid()) {
        LOG_ERROR(Engine, "Mesh primitive store full; cannot allocate single-mesh range for model ({})", model->name.c_str());
        return {};
    }

    uint32_t writeIndex = range.offset;
    for (uint32_t j = 0; j < count; ++j) {
        PrimitiveProperty& primitive = mesh.primitiveProperties[j];
        instances_[writeIndex] = {
            .primitiveIndex = primitive.index,
            .originalMaterialIndex = -1,
            .sourceNodeIndex = 0,
            .modelPrimitiveOrdinal = j,
            .materialID = material,
            .blasDeviceAddress = primitive.blasDeviceAddress,
            .modelSpaceTransform = Mat4(1.0f),
        };
        materialManager->AcquireMaterial(material);
        ++writeIndex;
    }
    return range;
}

void MeshPrimitiveStore::ReleaseAndFree(MaterialManager* materialManager, Range& range)
{
    if (!range.IsValid()) { return; }
    for (uint32_t i = 0; i < range.count; ++i) {
        materialManager->ReleaseMaterial(instances_[range.offset + i].materialID);
    }
    ranges_.Free(range);
    range = {};
}
} // Engine
