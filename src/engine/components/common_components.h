//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMMON_COMPONENTS_H
#define WILL_ENGINE_COMMON_COMPONENTS_H

#include <random>

#include <entt/entt.hpp>

#include "core/string_id.h"
#include "core/containers/inline_string.h"
#include "engine/component_registry.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
struct NameComponent
{
    static constexpr const char* COMPONENT_NAME = "NameComponent";

    Core::InlineString<128> name;

    WILL_REFLECT(NameComponent,
        WILL_FIELD(name))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};

struct DoNotSerializeTag
{
    static constexpr const char* COMPONENT_NAME = "DoNotSerializeTag";
};

/** Left out of runtime saves; its owner recreates it after a load. */
struct DoNotRuntimeSaveTag
{
};

struct PrefabInstanceComponent
{
    static constexpr const char* COMPONENT_NAME = "PrefabInstanceComponent";
    static constexpr bool INSTANCE_ONLY = true;

    StringID prefabId;

    WILL_REFLECT(PrefabInstanceComponent,
        WILL_FIELD(prefabId))
};
}

#endif //WILL_ENGINE_COMMON_COMPONENTS_H
