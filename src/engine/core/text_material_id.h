//
// Created by William on 2026-05-14.
//

#ifndef WILL_ENGINE_TEXT_MATERIAL_ID_H
#define WILL_ENGINE_TEXT_MATERIAL_ID_H

#include <cstdint>

#include "core/containers/hash.h"

namespace Engine
{
struct TextMaterialID
{
    uint64_t id = 0;

    constexpr TextMaterialID() = default;
    constexpr explicit TextMaterialID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(TextMaterialID other) const { return id == other.id; }
    constexpr bool operator!=(TextMaterialID other) const { return id != other.id; }
    constexpr bool operator<(TextMaterialID other)  const { return id < other.id; }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const TextMaterialID INVALID;
};

inline const TextMaterialID TextMaterialID::INVALID{};
} // Engine

namespace Core
{
template<> struct Hash<Engine::TextMaterialID> { uint64_t operator()(Engine::TextMaterialID m) const { return m.id; } };
}

#endif //WILL_ENGINE_TEXT_MATERIAL_ID_H
