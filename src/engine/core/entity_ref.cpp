//
// Created by William on 2026-10-22.
//

#include "entity_ref.h"

#include "engine/engine_api.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/systems/scene_system.h"

namespace Engine
{
entt::entity EntityRef::Get(const EngineState* state)
{
    if (state->registry.valid(cached)) {
        const auto* id = state->registry.try_get<Component::StableIdComponent>(cached);
        if (id && id->id == stableId) { return cached; }
    }
    cached = FindEntity(state, stableId);
    return cached;
}
} // Engine
