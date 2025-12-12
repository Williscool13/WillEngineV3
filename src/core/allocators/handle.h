//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_HANDLE_H
#define WILL_ENGINE_HANDLE_H
#include <cstdint>

namespace Core
{
inline static uint32_t INVALID_HANDLE_INDEX = 0xFFFFFF;
inline static uint32_t INVALID_HANDLE_GENERATION = 0xFF;

template<typename T>
struct Handle
{
    uint32_t index: 24;
    uint32_t generation: 8;

    [[nodiscard]] bool IsValid() const { return generation != INVALID_HANDLE_GENERATION; }
    bool operator==(Handle other) const { return index == other.index && generation == other.generation; }

    bool operator<(Handle other) const
    {
        if (index != other.index) return index < other.index;
        return generation < other.generation;
    }

    static const Handle Invalid;
};


template<typename T>
inline const Handle<T> Handle<T>::Invalid{
    INVALID_HANDLE_INDEX,
    INVALID_HANDLE_GENERATION
};
} // Core

#endif //WILL_ENGINE_HANDLE_H