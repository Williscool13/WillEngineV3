//
// Created by William on 2026-10-22.
//

#include "entity_ref.h"

#include "engine/systems/scene_system.h"

namespace Engine
{
entt::entity EntityRef::Get(const EngineState* state) const
{
    return FindEntity(state, stableId);
}
} // Engine
