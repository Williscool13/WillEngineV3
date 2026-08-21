//
// Created by William on 2026-08-21.
//

#ifndef WILL_ENGINE_VIRTUAL_MEMORY_MANAGER_H
#define WILL_ENGINE_VIRTUAL_MEMORY_MANAGER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "tlsf_allocator.h"
#include "core/containers/array.h"
#include "core/containers/inline_string.h"

namespace Core
{
/**
 * Address-space reservations with on-demand page commit. Thread-safe.
 *
 * Usage:
 *   Handle h = vm.Reserve(256 * 1024 * 1024, AllocTag::FrameSync0, "frame0");
 *   vm.EnsureCommitted(h, bytesNeeded);
 *   auto* p = static_cast<uint8_t*>(vm.Base(h));
 *   vm.Release(h);
 */
class VirtualMemoryManager
{
public:
    using Handle = uint32_t;
    static constexpr Handle INVALID_HANDLE = UINT32_MAX;
    static constexpr size_t MAX_RESERVATIONS = 32;
    static constexpr size_t COMMIT_STEP = 2ull * 1024 * 1024;

    struct Reservation
    {
        void* base{nullptr};
        size_t reserved{0};
        size_t committed{0};
        AllocTag tag{AllocTag::Unknown};
        InlineString<32> name{};
    };

    struct Stats
    {
        size_t reservedBytes;
        size_t committedBytes;
        size_t reservationCount;
    };

    VirtualMemoryManager() = default;

    ~VirtualMemoryManager();

    VirtualMemoryManager(const VirtualMemoryManager&) = delete;

    VirtualMemoryManager& operator=(const VirtualMemoryManager&) = delete;

    Handle Reserve(size_t bytes, AllocTag tag, const char* name);

    /** Rounds up to COMMIT_STEP. Asserts past reserved. */
    bool EnsureCommitted(Handle h, size_t bytes);

    /** keepBytes rounds up to COMMIT_STEP. */
    void Decommit(Handle h, size_t keepBytes);

    void Release(Handle h);

    [[nodiscard]] void* Base(Handle h) const;

    [[nodiscard]] size_t Reserved(Handle h) const;

    [[nodiscard]] size_t Committed(Handle h) const;

    [[nodiscard]] Stats GetStats() const;

    size_t GetReservations(Reservation out[], size_t maxOut) const;

private:
    struct Entry
    {
        void* base{nullptr};
        size_t reserved{0};
        std::atomic<size_t> committed{0};
        AllocTag tag{AllocTag::Unknown};
        InlineString<32> name{};
    };

    Array<Entry, MAX_RESERVATIONS> entries_{};
    mutable std::mutex mutex_;
};
} // Core

#endif //WILL_ENGINE_VIRTUAL_MEMORY_MANAGER_H
