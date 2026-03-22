//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMMON_COMPONENTS_H
#define WILL_ENGINE_COMMON_COMPONENTS_H

#include <random>

#include <entt/entt.hpp>

#include "core/string_id.h"
#include "core/stack_string.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct NameComponent
{
    StackString<256> name;

    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

struct DoNotSerializeTag
{};

struct PrefabInstanceComponent
{
    StringID prefabId;
};
}

#endif //WILL_ENGINE_COMMON_COMPONENTS_H
