//
// Created by William on 2026-03-12.
//

#ifndef WILL_ENGINE_MODEL_ID_H
#define WILL_ENGINE_MODEL_ID_H

#include <cstdint>
#include <functional>

#include "core/containers/hash.h"

namespace Engine
{
struct ModelID
{
    uint64_t id = 0;

    constexpr ModelID() = default;
    constexpr explicit ModelID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(ModelID other) const { return id == other.id; }
    constexpr bool operator!=(ModelID other) const { return id != other.id; }
    constexpr bool operator<(ModelID other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const ModelID INVALID;
};

inline const ModelID ModelID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::ModelID>
{
    size_t operator()(Engine::ModelID m) const noexcept
    {
        return m.id;
    }
};
}

namespace Core
{
template<> struct Hash<Engine::ModelID> { uint64_t operator()(Engine::ModelID m) const { return m.id; } };
}

#endif //WILL_ENGINE_MODEL_ID_H
