//
// Created by William on 2026-08-07.
//

#ifndef WILL_ENGINE_CONTAINER_UTILS_H
#define WILL_ENGINE_CONTAINER_UTILS_H

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace Core::Detail
{
/** Copy-constructs count elements into uninitialized dst; memcpy for trivially copyable T. Ranges must not overlap. */
template<typename T>
void CopyConstructRange(T* dst, const T* src, size_t count)
{
    if constexpr (std::is_trivially_copyable_v<T>) {
        if (count > 0) { memcpy(dst, src, count * sizeof(T)); }
    }
    else {
        for (size_t i = 0; i < count; ++i) { new(dst + i) T(src[i]); }
    }
}

/** Move-constructs count elements into uninitialized dst and destroys the sources; memcpy for trivially copyable T. Ranges must not overlap. */
template<typename T>
void RelocateRange(T* dst, T* src, size_t count)
{
    if constexpr (std::is_trivially_copyable_v<T>) {
        if (count > 0) { memcpy(dst, src, count * sizeof(T)); }
    }
    else {
        for (size_t i = 0; i < count; ++i) {
            new(dst + i) T(std::move(src[i]));
            src[i].~T();
        }
    }
}

/** Value-constructs count elements (zero-fill for trivial T), matching placement-new T() semantics. */
template<typename T>
void ValueConstructRange(T* dst, size_t count)
{
    if constexpr (std::is_trivial_v<T>) {
        if (count > 0) { memset(dst, 0, count * sizeof(T)); }
    }
    else {
        for (size_t i = 0; i < count; ++i) { new(dst + i) T(); }
    }
}

/** Destroys count elements; no-op for trivially destructible T (skips the loop entirely in debug builds too). */
template<typename T>
void DestroyRange(T* data, size_t count)
{
    if constexpr (!std::is_trivially_destructible_v<T>) {
        for (size_t i = 0; i < count; ++i) { data[i].~T(); }
    }
    else {
        (void)data;
        (void)count;
    }
}
} // namespace Core::Detail

#endif // WILL_ENGINE_CONTAINER_UTILS_H
