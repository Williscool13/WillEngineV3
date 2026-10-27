//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_ENTITY_REF_H
#define WILL_ENGINE_ENTITY_REF_H

#include <entt/entt.hpp>

#include "core/string_id.h"

namespace Engine
{
struct EngineState;

/** A reference to another entity, saved as its stable id. */
struct EntityRef
{
    StringID stableId;

    entt::entity Get(const EngineState* state) const;

    bool operator==(const EntityRef& other) const { return stableId == other.stableId; }
};
} // Engine

#endif //WILL_ENGINE_ENTITY_REF_H
