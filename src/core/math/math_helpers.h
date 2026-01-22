//
// Created by William on 2026-01-23.
//

#ifndef WILL_ENGINE_MATH_HELPERS_H
#define WILL_ENGINE_MATH_HELPERS_H
#include <cstdint>
#include <bit>

namespace Core::Math
{
constexpr size_t NextPowerOfTwo(size_t number)
{
    if (number == 0) return 1;
    if (std::has_single_bit(number)) return number;
    return std::bit_ceil(number);
}
} // Core::Math
using Core::Math::NextPowerOfTwo;
#endif //WILL_ENGINE_MATH_HELPERS_H
