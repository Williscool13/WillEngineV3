//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMMON_COMPONENTS_H
#define WILL_ENGINE_COMMON_COMPONENTS_H

#include <random>
#include <json/nlohmann/json_fwd.hpp>

#include "core/string_id.h"

namespace Game::Component
{
struct StableIdComponent
{
    StringID id;

    static void Serialize(const StableIdComponent& comp, nlohmann::json& json);
    static void Deserialize(StableIdComponent& comp, const nlohmann::json& json);

    static StringID Generate(std::mt19937_64& rng)
    {
        return StringID(rng());
    }
};

struct NameComponent
{
    std::string name;

    static void Serialize(const NameComponent& comp, nlohmann::json& json);
    static void Deserialize(NameComponent& comp, const nlohmann::json& json);
};
}

#endif //WILL_ENGINE_COMMON_COMPONENTS_H
