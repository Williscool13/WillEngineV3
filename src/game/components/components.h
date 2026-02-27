//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENTS_H
#define WILL_ENGINE_COMPONENTS_H

#include <random>

#include "core/component_registry.h"

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

void RegisterComponents(Core::ComponentRegistry& componentRegistry);
}

#endif //WILL_ENGINE_COMPONENTS_H
