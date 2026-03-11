//
// Created by William on 2026-03-11.
//

#ifndef WILL_ENGINE_SAMPLER_ID_H
#define WILL_ENGINE_SAMPLER_ID_H

#include <cstdint>
#include <functional>

namespace Engine
{
struct SamplerID
{
    uint64_t id = 0;

    constexpr SamplerID() = default;
    constexpr explicit SamplerID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(SamplerID other) const { return id == other.id; }
    constexpr bool operator!=(SamplerID other) const { return id != other.id; }
    constexpr bool operator<(SamplerID other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const SamplerID INVALID;
};

inline const SamplerID SamplerID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::SamplerID>
{
    size_t operator()(Engine::SamplerID s) const noexcept
    {
        return s.id;
    }
};
}

#endif //WILL_ENGINE_SAMPLER_ID_H
