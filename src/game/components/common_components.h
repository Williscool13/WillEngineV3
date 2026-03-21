//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMMON_COMPONENTS_H
#define WILL_ENGINE_COMMON_COMPONENTS_H

#include <random>

#include "core/string_id.h"

namespace Game::Component
{
struct StableIdComponent
{
    StringID id;

    static StringID Generate(std::mt19937_64& rng)
    {
        return StringID(rng());
    }
};

struct NameComponent
{
    std::string name;
};

struct DoNotSerializeTag
{};

struct PrefabInstanceComponent
{
    StringID prefabId;
};
}

#endif //WILL_ENGINE_COMMON_COMPONENTS_H
