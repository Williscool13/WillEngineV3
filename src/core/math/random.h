//
// Created by William on 2026-10-18.
//

#ifndef WILL_ENGINE_RANDOM_H
#define WILL_ENGINE_RANDOM_H

#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

#include "core/math/constants.h"

namespace Core
{
/** xoshiro256** seeded through SplitMix64. Satisfies UniformRandomBitGenerator, so it works with std::ranges::shuffle. */
class Random
{
public:
    using result_type = uint64_t;

    /** Seeded from std::random_device. */
    Random();

    explicit Random(uint64_t seed)
    {
        for (uint64_t& s : state) {
            seed += 0x9E3779B97F4A7C15ull;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            s = z ^ (z >> 31);
        }
    }

    static constexpr uint64_t min() { return 0; }
    static constexpr uint64_t max() { return std::numeric_limits<uint64_t>::max(); }

    uint64_t operator()() { return NextU64(); }

    uint64_t NextU64()
    {
        const uint64_t result = Rotl(state[1] * 5, 7) * 9;
        const uint64_t t = state[1] << 17;
        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];
        state[2] ^= t;
        state[3] = Rotl(state[3], 45);
        return result;
    }

    uint32_t NextU32() { return static_cast<uint32_t>(NextU64() >> 32); }

    /** [0, 1) */
    float Float() { return static_cast<float>(NextU64() >> 40) * 0x1.0p-24f; }

    /** [min, max); min when the range is empty. */
    float Range(float min, float max)
    {
        if (!(max > min)) { return min; }
        const float v = min + (max - min) * Float();
        return v < max ? v : std::nextafter(max, min);
    }

    /** [min, max); min when the range is empty. */
    int32_t Range(int32_t min, int32_t max)
    {
        if (max <= min) { return min; }
        const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(max) - min);
        return static_cast<int32_t>(min + static_cast<int64_t>((NextU32() * span) >> 32));
    }

    bool Chance(float probability) { return Float() < probability; }

    uint64_t GetState(int i) const { return state[i]; }
    void SetState(int i, uint64_t v) { state[i] = v; }

    /** Uniform on the unit sphere. */
    glm::vec3 UnitVector()
    {
        const float z = Range(-1.0f, 1.0f);
        const float phi = Math::TAU * Float();
        const float r = std::sqrt(1.0f - z * z);
        return {r * std::cos(phi), r * std::sin(phi), z};
    }

    /** Uniform inside the unit ball. */
    glm::vec3 InsideUnitSphere() { return UnitVector() * std::cbrt(Float()); }

private:
    static uint64_t Rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t state[4];
};
} // Core

#endif //WILL_ENGINE_RANDOM_H
