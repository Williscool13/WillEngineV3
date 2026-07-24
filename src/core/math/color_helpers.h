//
// Created by William on 2026-07-24.
//

#ifndef WILL_ENGINE_COLOR_HELPERS_H
#define WILL_ENGINE_COLOR_HELPERS_H
#include <cstdint>

#include "core/types/math.h"

namespace Core::Math
{
/**
 * Stable debug color from an arbitrary key (Knuth multiplicative hash). Same key = same color across sessions.
 * @param salt Decorrelates domains that hash the same key space; leave 0 when only one domain draws the keys.
 * @param hueMin / hueSpan Restrict the hue wheel in [0,1] turns, e.g. to keep hashed colors off a band reserved for a fixed indicator.
 */
inline Vec4 HashColor(uint64_t key, uint32_t salt = 0u, float hueMin = 0.0f, float hueSpan = 1.0f)
{
    const uint32_t h = (static_cast<uint32_t>(key ^ (key >> 32)) ^ salt) * 2654435761u;
    const float hue = (hueMin + static_cast<float>(h >> 8) / 16777216.0f * hueSpan) * 6.0f;
    const int sector = static_cast<int>(hue) % 6;
    const float f = hue - static_cast<float>(static_cast<int>(hue));
    constexpr float V = 1.0f;
    constexpr float P = 0.25f;
    const float q = V - (V - P) * f;
    const float t = P + (V - P) * f;
    switch (sector) {
        case 0: { return {V, t, P, 1.0f}; }
        case 1: { return {q, V, P, 1.0f}; }
        case 2: { return {P, V, t, 1.0f}; }
        case 3: { return {P, q, V, 1.0f}; }
        case 4: { return {t, P, V, 1.0f}; }
        default: { return {V, P, q, 1.0f}; }
    }
}
} // Core::Math
#endif //WILL_ENGINE_COLOR_HELPERS_H
