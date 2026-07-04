//
// Created by William on 2026-07-04.
//

#ifndef WILL_ENGINE_ACTION_HANDLE_H
#define WILL_ENGINE_ACTION_HANDLE_H

#include <cstdint>
#include <functional>

#include "core/containers/hash.h"

namespace Engine
{
struct ActionHandle
{
    uint64_t id = 0;

    constexpr ActionHandle() = default;
    constexpr explicit ActionHandle(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(ActionHandle other) const { return id == other.id; }
    constexpr bool operator!=(ActionHandle other) const { return id != other.id; }
    constexpr bool operator<(ActionHandle other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const ActionHandle INVALID;
};

inline const ActionHandle ActionHandle::INVALID{};
}

namespace std
{
template<>
struct hash<Engine::ActionHandle>
{
    size_t operator()(Engine::ActionHandle a) const noexcept
    {
        return a.id;
    }
};
}

namespace Core
{
template<> struct Hash<Engine::ActionHandle> { uint64_t operator()(Engine::ActionHandle a) const { return a.id; } };
}

#endif //WILL_ENGINE_ACTION_HANDLE_H
