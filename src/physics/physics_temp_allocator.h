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

#include "core/memory/virtual_memory_manager.h"

namespace Physics
{
/**
 * Jolt stack allocator over a VirtualMemoryManager reservation. Pages commit as the top advances. Single-threaded per update.
 */
class PhysicsTempAllocator final : public JPH::TempAllocator
{
public:
    PhysicsTempAllocator() = default;

    PhysicsTempAllocator(Core::VirtualMemoryManager& vm, size_t reserveBytes)
        : vm_(&vm), handle_(vm.Reserve(reserveBytes, Core::AllocTag::Physics, "physics"))
    {
        mBase = static_cast<uint8_t*>(vm.Base(handle_));
        mSize = vm.Reserved(handle_);
    }

    ~PhysicsTempAllocator() override
    {
        assert(mTop == 0 && "PhysicsTempAllocator: leaked allocations at destruction");
        if (vm_) {
            vm_->Release(handle_);
        }
    }

    PhysicsTempAllocator(const PhysicsTempAllocator&) = delete;

    PhysicsTempAllocator& operator=(const PhysicsTempAllocator&) = delete;

    void* Allocate(JPH::uint inSize) override
    {
        if (inSize == 0) { return nullptr; }
        const size_t aligned = AlignUp(inSize);
        assert(mTop + aligned <= mSize && "PhysicsTempAllocator: out of memory");
        void* ptr = mBase + mTop;
        mTop += aligned;
        if (mTop > mCommitted) {
            vm_->EnsureCommitted(handle_, mTop);
            mCommitted = vm_->Committed(handle_);
        }
        return ptr;
    }

    void Free(void* inAddress, JPH::uint inSize) override
    {
        if (!inAddress) { return; }
        mTop -= AlignUp(inSize);
        assert(mBase + mTop == inAddress && "PhysicsTempAllocator: out-of-order free");
    }

    [[nodiscard]] size_t GetUsed() const { return mTop; }

private:
    static size_t AlignUp(size_t size)
    {
        return (size + JPH_RVECTOR_ALIGNMENT - 1) & ~(size_t(JPH_RVECTOR_ALIGNMENT) - 1);
    }

    Core::VirtualMemoryManager* vm_{};
    Core::VirtualMemoryManager::Handle handle_{Core::VirtualMemoryManager::INVALID_HANDLE};
    uint8_t* mBase{};
    size_t mSize{};
    size_t mTop{};
    size_t mCommitted{};
};
} // Physics

#endif //WILL_ENGINE_PHYSICS_TEMP_ALLOCATOR_H
