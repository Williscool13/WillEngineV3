//
// Created by William on 2026-10-09.
//

#ifndef WILL_ENGINE_MOBILITY_ASSIGN_H
#define WILL_ENGINE_MOBILITY_ASSIGN_H

#include <cstdint>

#include <entt/entt.hpp>

namespace Engine
{
struct EngineState;

struct MobilityAssignResult
{
    uint32_t staticCount{0};
    uint32_t movableCount{0};
    uint32_t lockedCount{0};
    uint32_t changedCount{0};
    uint32_t bakedLightsOnMovers{0};
};

/** True when the entity or an ancestor has a moving physics body or a MOVES_ENTITY component. */
bool IsMovedAtRuntime(const EngineState* state, entt::entity entity);

/** Sets every unlocked transform's mobility from IsMovedAtRuntime. */
MobilityAssignResult AssignMobility(EngineState* state);

/** Locks every Static transform nothing is expected to move; PropagateDirtyTransforms reverts writes to them during play. */
void LockStaticTransforms(EngineState* state);

/** Reverts play-time writes to locked Static transforms and drops their dirty tags; warns once per entity. */
void RejectStaticTransformWrites(EngineState* state);
} // Engine

#endif //WILL_ENGINE_MOBILITY_ASSIGN_H
