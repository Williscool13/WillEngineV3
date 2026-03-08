//
// Created by William on 2026-03-08.
//

#ifndef WILL_ENGINE_MATERIAL_ID_H
#define WILL_ENGINE_MATERIAL_ID_H

#include <cstdint>
#include <functional>

namespace Engine
{
struct MaterialID
{
    uint64_t id = 0;

    constexpr MaterialID() = default;
    constexpr explicit MaterialID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(MaterialID other) const { return id == other.id; }
    constexpr bool operator!=(MaterialID other) const { return id != other.id; }
    constexpr bool operator<(MaterialID other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const MaterialID INVALID;
};

inline const MaterialID MaterialID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::MaterialID>
{
    size_t operator()(Engine::MaterialID m) const noexcept
    {
        return m.id;
    }
};
}

#endif //WILL_ENGINE_MATERIAL_ID_H