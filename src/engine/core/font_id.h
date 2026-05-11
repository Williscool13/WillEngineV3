//
// Created by William on 2026-05-11.
//

#ifndef WILL_ENGINE_FONT_ID_H
#define WILL_ENGINE_FONT_ID_H

#include <cstdint>
#include <functional>

#include "core/containers/hash.h"

namespace Engine
{
struct FontID
{
    uint64_t id = 0;

    constexpr FontID() = default;
    constexpr explicit FontID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(FontID other) const { return id == other.id; }
    constexpr bool operator!=(FontID other) const { return id != other.id; }
    constexpr bool operator<(FontID other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const FontID INVALID;
};

inline const FontID FontID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::FontID>
{
    size_t operator()(Engine::FontID f) const noexcept
    {
        return f.id;
    }
};
}

namespace Core
{
template<> struct Hash<Engine::FontID> { uint64_t operator()(Engine::FontID f) const { return f.id; } };
}

#endif //WILL_ENGINE_FONT_ID_H
