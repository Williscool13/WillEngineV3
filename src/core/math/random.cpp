//
// Created by William on 2026-10-18.
//

#include "random.h"

#include <random>

namespace Core
{
static uint64_t EntropySeed()
{
    std::random_device device;
    return (static_cast<uint64_t>(device()) << 32) | device();
}

Random::Random()
    : Random(EntropySeed())
{}
} // Core
