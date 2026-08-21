//
// Created by William on 2026-08-21.
//

#ifndef WILL_ENGINE_VIRTUAL_ARRAY_H
#define WILL_ENGINE_VIRTUAL_ARRAY_H

#include <algorithm>
#include <cassert>
#include <cstddef>

#include "core/containers/container_utils.h"
#include "core/memory/virtual_memory_manager.h"

namespace Core
{
/**
 * Fixed-capacity array on a VirtualMemoryManager reservation. Slots past Committed() are not constructed; indexing them asserts.
 */
template<typename T>
class VirtualArray
{
public:
    VirtualArray() = default;

    VirtualArray(VirtualMemoryManager* vm, AllocTag tag, size_t capacity, const char* name)
        : vm_(vm), capacity_(capacity)
    {
        assert(vm_ != nullptr && "VirtualArray: no manager");
        assert(capacity_ > 0 && "VirtualArray: capacity must be > 0");
        handle_ = vm_->Reserve(capacity_ * sizeof(T), tag, name);
        data_ = static_cast<T*>(vm_->Base(handle_));
    }

    ~VirtualArray() { Reset(); }

    VirtualArray(const VirtualArray&) = delete;

    VirtualArray& operator=(const VirtualArray&) = delete;

    VirtualArray(VirtualArray&& other) noexcept
        : vm_(other.vm_), handle_(other.handle_), data_(other.data_), capacity_(other.capacity_), committed_(other.committed_)
    {
        other.Detach();
    }

    VirtualArray& operator=(VirtualArray&& other) noexcept
    {
        if (this == &other) { return *this; }
        Reset();
        vm_ = other.vm_;
        handle_ = other.handle_;
        data_ = other.data_;
        capacity_ = other.capacity_;
        committed_ = other.committed_;
        other.Detach();
        return *this;
    }

    void Reset()
    {
        if (data_) {
            Detail::DestroyRange(data_, committed_);
            vm_->Release(handle_);
        }
        Detach();
    }

    /** Commits and value-constructs [committed, count). */
    void EnsureCommitted(size_t count)
    {
        if (count <= committed_) { return; }
        assert(count <= capacity_ && "VirtualArray: past capacity");
        vm_->EnsureCommitted(handle_, count * sizeof(T));
        const size_t newCommitted = std::min(vm_->Committed(handle_) / sizeof(T), capacity_);
        Detail::ValueConstructRange(data_ + committed_, newCommitted - committed_);
        committed_ = newCommitted;
    }

    /** Decommits above keepCount + one COMMIT_STEP of slack. */
    void Trim(size_t keepCount)
    {
        const size_t keepBytes = keepCount * sizeof(T) + VirtualMemoryManager::COMMIT_STEP;
        if (keepBytes >= committed_ * sizeof(T)) { return; }
        vm_->Decommit(handle_, keepBytes);
        const size_t newCommitted = std::min(vm_->Committed(handle_) / sizeof(T), capacity_);
        Detail::DestroyRange(data_ + newCommitted, committed_ - newCommitted);
        committed_ = newCommitted;
    }

    T& operator[](size_t i)
    {
        assert(i < committed_);
        return data_[i];
    }

    const T& operator[](size_t i) const
    {
        assert(i < committed_);
        return data_[i];
    }

    T* Data() { return data_; }
    const T* Data() const { return data_; }

    size_t Capacity() const { return capacity_; }
    size_t Committed() const { return committed_; }
    bool IsAllocated() const { return data_ != nullptr; }

private:
    void Detach()
    {
        vm_ = nullptr;
        handle_ = VirtualMemoryManager::INVALID_HANDLE;
        data_ = nullptr;
        capacity_ = 0;
        committed_ = 0;
    }

    VirtualMemoryManager* vm_{nullptr};
    VirtualMemoryManager::Handle handle_{VirtualMemoryManager::INVALID_HANDLE};
    T* data_{nullptr};
    size_t capacity_{0};
    size_t committed_{0};
};
} // Core

#endif //WILL_ENGINE_VIRTUAL_ARRAY_H
