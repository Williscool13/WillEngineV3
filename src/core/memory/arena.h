//
// Created by William on 2026-03-29.
//

#ifndef WILL_ENGINE_ARENA_H
#define WILL_ENGINE_ARENA_H

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>

#include "core/containers/inline_string.h"

namespace Core
{
class VirtualMemoryManager;

/**
 * Non-owning bump-pointer allocator over an externally-provided buffer.
 * Does NOT call destructors on Reset(); only use for trivially-destructible
 * types, or manage lifetimes manually.
 */
class Arena
{
public:
    Arena() = default;

    Arena(void* memory, size_t size, const char* name);

    /** size is the reserved range; pages commit as the head advances. */
    Arena(void* memory, size_t size, const char* name, VirtualMemoryManager* vm, uint32_t vmHandle);

    Arena(const Arena&) = delete;

    Arena& operator=(const Arena&) = delete;

    Arena(Arena&&) = default;

    Arena& operator=(Arena&&) = default;

    /**
     * Allocates sizeof(T), aligned to alignof(T), and constructs T with the given args.
     * Returns nullptr if out of memory (asserts in debug).
     */
    template<typename T, typename... Args>
    T* Alloc(Args&&... args);

    /**
     * Allocates count * sizeof(T), aligned to alignof(T).
     * Default-constructs each element for non-trivial types; leaves trivial types uninitialized.
     * Returns nullptr if out of memory (asserts in debug).
     */
    template<typename T>
    T* AllocArray(size_t count);

    /**
     * Raw allocation. Returns nullptr if out of memory (asserts in debug).
     */
    void* AllocRaw(size_t size, size_t alignment = alignof(std::max_align_t));

    /**
     * Resets the bump pointer to the start. Does not call destructors.
     */
    void Reset();

    /**
     * Decommits above keepBytes (rounded up to COMMIT_STEP). head must not exceed keepBytes. No-op without a VirtualMemoryManager.
     * @param keepBytes
     */
    void Trim(size_t keepBytes);

    struct Stats
    {
        size_t totalBytes;
        size_t usedBytes;
        size_t freeBytes;
        size_t peakBytes;
        size_t committedBytes;
    };

    [[nodiscard]] Stats GetStats() const { return {capacity, head, capacity - head, peakHead, committed}; }
    [[nodiscard]] void* Data() const { return memory; }
    [[nodiscard]] size_t GetUsed() const { return head; }
    [[nodiscard]] size_t GetCapacity() const { return capacity; }
    [[nodiscard]] size_t GetRemaining() const { return capacity - head; }
    [[nodiscard]] size_t GetPeak() const { return peakHead; }
    [[nodiscard]] const char* GetName() const { return name.buf; }

private:
    void* memory{};
    size_t head{};
    size_t capacity{};
    size_t committed{};
    VirtualMemoryManager* vm{};
    uint32_t vmHandle{};
    size_t peakHead{};
    InlineString<32> name{};
};

template<typename T, typename... Args>
T* Arena::Alloc(Args&&... args)
{
    void* ptr = AllocRaw(sizeof(T), alignof(T));
    if (!ptr) { return nullptr; }
    return new(ptr) T(std::forward<Args>(args)...);
}

template<typename T>
T* Arena::AllocArray(size_t count)
{
    assert(count > 0);
    void* ptr = AllocRaw(sizeof(T) * count, alignof(T));
    if (!ptr) { return nullptr; }
    if constexpr (!std::is_trivially_constructible_v<T>) {
        T* arr = static_cast<T*>(ptr);
        for (size_t i = 0; i < count; ++i) { new(arr + i) T(); }
    }
    return static_cast<T*>(ptr);
}
} // Core

#endif //WILL_ENGINE_ARENA_H
