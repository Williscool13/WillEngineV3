//
// Created by William on 2026-10-06.
//

#ifndef WILL_ENGINE_EXTENT_H
#define WILL_ENGINE_EXTENT_H

#include <cstdint>

namespace Core
{
struct Extent2D
{
    uint32_t width{0};
    uint32_t height{0};

    bool operator==(const Extent2D&) const = default;
};
} // Core

#endif //WILL_ENGINE_EXTENT_H
