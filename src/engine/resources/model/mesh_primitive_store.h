//
// Created by William on 2026-07-01.
//

#ifndef WILL_ENGINE_MESH_PRIMITIVE_STORE_H
#define WILL_ENGINE_MESH_PRIMITIVE_STORE_H

#include <cstdint>

#include "core/containers/heap_array.h"
#include "core/memory/range_allocator.h"
#include "core/types/math.h"
#include "engine/core/material_id.h"

namespace Engine
{
struct MeshPrimitiveInstance
{
    uint32_t primitiveIndex{0};
    int32_t originalMaterialIndex{-1};
    uint32_t sourceNodeIndex{~0u};
    uint32_t modelPrimitiveOrdinal{~0u};
    MaterialID materialID{};
    uint64_t blasDeviceAddress{0};
    Mat4 modelSpaceTransform{1.0f};
};

inline constexpr uint32_t MAX_MESH_PRIMITIVE_INSTANCES = 256 * 1024;


/**
 * Shared store of flattened mesh primitives (static, static-primitive, procedural, spline, text3d). A RangeAllocator hands out one contiguous run per entity. Callers own material lifetimes and GPU uploads. Not thread-safe.
 */
class MeshPrimitiveStore
{
public:
    using Range = Core::RangeAllocator::Range;

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag = Core::AllocTag::RenderMesh);

    Range Allocate(uint32_t count) { return ranges_.Allocate(count); }

    void Free(Range range) { ranges_.Free(range); }

    MeshPrimitiveInstance* Get(Range range) { return range.IsValid() ? &instances_[range.offset] : nullptr; }

    MeshPrimitiveInstance& operator[](uint32_t i) { return instances_[i]; }
    const MeshPrimitiveInstance& operator[](uint32_t i) const { return instances_[i]; }

    MeshPrimitiveInstance* Data() { return instances_.Data(); }
    const MeshPrimitiveInstance* Data() const { return instances_.Data(); }

    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    Core::HeapArray<MeshPrimitiveInstance> instances_{};
    Core::RangeAllocator ranges_{};
};
} // Engine

#endif //WILL_ENGINE_MESH_PRIMITIVE_STORE_H
