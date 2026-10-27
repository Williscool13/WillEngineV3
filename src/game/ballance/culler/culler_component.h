//
// Created by William on 2026-10-27.
//

#ifndef WILL_ENGINE_BALLANCE_CULLER_COMPONENT_H
#define WILL_ENGINE_BALLANCE_CULLER_COMPONENT_H

#include <cstdint>

#include "engine/component_registry.h"
#include "engine/reflection/reflection.h"
#include "game/ballance/fwd_components.h"

namespace Ballance::Component
{
/** Shrinks away the longest-sleeping shards over the budget, and any non-player dynamic body below `killHeight` (world) or past `killRadius` (horizontal, from this entity). */
struct CullerComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "CullerComponent";

    int32_t sleepingShardBudget{600};
    float killHeight{-35.0f};
    float killRadius{40.0f};
    float cullSeconds{0.5f};

    WILL_REFLECT(CullerComponent,
        WILL_FIELD(sleepingShardBudget),
        WILL_FIELD(killHeight, .speed = 0.1f),
        WILL_FIELD(killRadius, .min = 0.0f, .speed = 0.1f),
        WILL_FIELD(cullSeconds, .min = 0.05f, .speed = 0.05f))
};

/** Run seconds when the shard fell asleep. */
struct ShardSleepRuntime
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "ShardSleepRuntime";

    float sleepSince{0.0f};

    WILL_REFLECT(ShardSleepRuntime, WILL_FIELD(sleepSince))
};
}

#endif //WILL_ENGINE_BALLANCE_CULLER_COMPONENT_H
