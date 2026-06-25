//
// Created by William on 2026-04-11.
//

#ifndef WILL_ENGINE_PHYSICS_TEMP_ALLOCATOR_H
#define WILL_ENGINE_PHYSICS_TEMP_ALLOCATOR_H

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>

namespace Physics
{
class PhysicsTempAllocator final : public JPH::TempAllocator
{
public:
    PhysicsTempAllocator() = default;

    PhysicsTempAllocator(void* mem, size_t size)
    {
        // Jolt SIMD types needsd alignment, backing buffer not necessarily aligned
        const uintptr_t raw = reinterpret_cast<uintptr_t>(mem);
        const uintptr_t aligned = (raw + JPH_RVECTOR_ALIGNMENT - 1) & ~(uintptr_t(JPH_RVECTOR_ALIGNMENT) - 1);
        const size_t adjust = aligned - raw;
        mBase = reinterpret_cast<uint8_t*>(aligned);
        mSize = size > adjust ? size - adjust : 0;
    }

    ~PhysicsTempAllocator() override
    {
        assert(mTop == 0 && "PhysicsTempAllocator: leaked allocations at destruction");
    }

    void* Allocate(JPH::uint inSize) override
    {
        if (inSize == 0) { return nullptr; }
        const size_t aligned = AlignUp(inSize);
        assert(mTop + aligned <= mSize && "PhysicsTempAllocator: out of memory");
        void* ptr = mBase + mTop;
        mTop += aligned;
        return ptr;
    }

    void Free(void* inAddress, JPH::uint inSize) override
    {
        if (!inAddress) { return; }
        mTop -= AlignUp(inSize);
        assert(mBase + mTop == inAddress && "PhysicsTempAllocator: out-of-order free");
    }

private:
    static size_t AlignUp(size_t size)
    {
        return (size + JPH_RVECTOR_ALIGNMENT - 1) & ~(size_t(JPH_RVECTOR_ALIGNMENT) - 1);
    }

    uint8_t* mBase{};
    size_t mSize{};
    size_t mTop{};
};
} // Physics

#endif //WILL_ENGINE_PHYSICS_TEMP_ALLOCATOR_H
