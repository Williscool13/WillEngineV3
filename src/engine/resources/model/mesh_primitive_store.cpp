//
// Created by William on 2026-07-01.
//

#include "engine/resources/model/mesh_primitive_store.h"

namespace Engine
{
void MeshPrimitiveStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag)
{
    instances_ = Core::HeapArray<MeshPrimitiveInstance>(alloc, tag, capacity);
    ranges_.Init(capacity, alloc, tag);
}
} // Engine
