//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_STABLE_ID_COMPONENT_H
#define WILL_ENGINE_STABLE_ID_COMPONENT_H

#include <random>

#include <entt/entt.hpp>

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

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

}

#endif //WILL_ENGINE_STABLE_ID_COMPONENT_H
