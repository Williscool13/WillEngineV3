//
// Created by William on 2026-10-08.
//

#ifndef WILL_ENGINE_TEXTURE_ARRAY_ID_H
#define WILL_ENGINE_TEXTURE_ARRAY_ID_H

#include <cstdint>
#include <functional>

#include "core/containers/hash.h"

namespace Engine
{
struct TextureArrayID
{
    uint64_t id = 0;

    constexpr TextureArrayID() = default;
    constexpr explicit TextureArrayID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(TextureArrayID other) const { return id == other.id; }
    constexpr bool operator!=(TextureArrayID other) const { return id != other.id; }
    constexpr bool operator<(TextureArrayID other) const { return id < other.id; }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const TextureArrayID INVALID;
};

inline const TextureArrayID TextureArrayID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::TextureArrayID>
{
    size_t operator()(Engine::TextureArrayID e) const noexcept
    {
        return e.id;
    }
};
}

namespace Core
{
template<> struct Hash<Engine::TextureArrayID> { uint64_t operator()(Engine::TextureArrayID e) const { return e.id; } };
}

#endif //WILL_ENGINE_TEXTURE_ARRAY_ID_H
