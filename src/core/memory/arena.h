//
// Created by William on 2026-03-29.
//

#ifndef WILL_ENGINE_ARENA_H
#define WILL_ENGINE_ARENA_H

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace Core
{
/**
 * Owning linear allocator. One malloc at construction, bump-pointer sub-allocation thereafter.
 * Does NOT call destructors on Reset() or destruction, only use for trivially-destructible
 * types, or manage lifetimes manually.
 */
class Arena
{
public:
    explicit Arena(size_t size);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

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
     * Resets the arena to empty. Does not free backing memory. Does not call destructors.
     */
    void Reset();

    [[nodiscard]] size_t GetUsed() const { return head; }
    [[nodiscard]] size_t GetCapacity() const { return capacity; }
    [[nodiscard]] size_t GetRemaining() const { return capacity - head; }

private:
    void* memory = nullptr;
    size_t head = 0;
    size_t capacity = 0;
};

template<typename T, typename... Args>
T* Arena::Alloc(Args&&... args)
{
    void* ptr = AllocRaw(sizeof(T), alignof(T));
    if (ptr == nullptr) {
        return nullptr;
    }
    return new(ptr) T(std::forward<Args>(args)...);
}

template<typename T>
T* Arena::AllocArray(size_t count)
{
    assert(count > 0);
    void* ptr = AllocRaw(sizeof(T) * count, alignof(T));
    if (ptr == nullptr) {
        return nullptr;
    }
    if constexpr (!std::is_trivially_constructible_v<T>) {
        T* arr = static_cast<T*>(ptr);
        for (size_t i = 0; i < count; ++i) {
            new(arr + i) T();
        }
    }
    return static_cast<T*>(ptr);
}
} // Core

#endif //WILL_ENGINE_ARENA_H
