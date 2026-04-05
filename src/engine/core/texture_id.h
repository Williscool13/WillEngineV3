//
// Created by William on 2026-03-09.
//

#ifndef WILL_ENGINE_TEXTURE_ID_H
#define WILL_ENGINE_TEXTURE_ID_H

#include <cstdint>
#include <functional>

#include "core/containers/hash.h"

namespace Engine
{
struct TextureID
{
    uint64_t id = 0;

    constexpr TextureID() = default;
    constexpr explicit TextureID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(TextureID other) const { return id == other.id; }
    constexpr bool operator!=(TextureID other) const { return id != other.id; }
    constexpr bool operator<(TextureID other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const TextureID INVALID;
};

inline const TextureID TextureID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::TextureID>
{
    size_t operator()(Engine::TextureID t) const noexcept
    {
        return t.id;
    }
};
}

namespace Core
{
template<> struct Hash<Engine::TextureID> { uint64_t operator()(Engine::TextureID t) const { return t.id; } };
}

#endif //WILL_ENGINE_TEXTURE_ID_H
