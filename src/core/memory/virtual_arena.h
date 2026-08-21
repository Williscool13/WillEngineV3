//
// Created by William on 2026-08-21.
//

#ifndef WILL_ENGINE_VIRTUAL_ARENA_H
#define WILL_ENGINE_VIRTUAL_ARENA_H

#include <utility>

#include "arena.h"
#include "virtual_memory_manager.h"

namespace Core
{
/**
 * RAII owner of an Arena over a VirtualMemoryManager reservation.
 */
class VirtualArena
{
public:
    VirtualArena() = default;

    VirtualArena(VirtualMemoryManager& vm, size_t reserveBytes, AllocTag tag, const char* name)
        : vm_(&vm), handle_(vm.Reserve(reserveBytes, tag, name)), arena_(vm.Base(handle_), vm.Reserved(handle_), name, &vm, handle_)
    {}

    ~VirtualArena() { Release(); }

    VirtualArena(const VirtualArena&) = delete;

    VirtualArena& operator=(const VirtualArena&) = delete;

    VirtualArena(VirtualArena&& other) noexcept
        : vm_(other.vm_), handle_(other.handle_), arena_(std::move(other.arena_))
    {
        other.vm_ = nullptr;
        other.handle_ = VirtualMemoryManager::INVALID_HANDLE;
    }

    VirtualArena& operator=(VirtualArena&& other) noexcept
    {
        if (this != &other) {
            Release();
            vm_ = other.vm_;
            handle_ = other.handle_;
            arena_ = std::move(other.arena_);
            other.vm_ = nullptr;
            other.handle_ = VirtualMemoryManager::INVALID_HANDLE;
        }
        return *this;
    }

    void Release()
    {
        if (vm_ && handle_ != VirtualMemoryManager::INVALID_HANDLE) {
            vm_->Release(handle_);
        }
        vm_ = nullptr;
        handle_ = VirtualMemoryManager::INVALID_HANDLE;
        arena_ = Arena{};
    }

    [[nodiscard]] Arena& Get() { return arena_; }
    [[nodiscard]] const Arena& Get() const { return arena_; }
    [[nodiscard]] bool IsValid() const { return arena_.Data() != nullptr; }

private:
    VirtualMemoryManager* vm_{nullptr};
    VirtualMemoryManager::Handle handle_{VirtualMemoryManager::INVALID_HANDLE};
    Arena arena_{};
};
} // Core

#endif //WILL_ENGINE_VIRTUAL_ARENA_H
