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

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct NameComponent
{
    static constexpr const char* COMPONENT_NAME = "NameComponent";

    Core::InlineString<128> name;

    static void Serialize(const NameComponent& comp, Engine::TextWriter& w);
    static void Deserialize(NameComponent& comp, const Engine::TextReader& r);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

struct DoNotSerializeTag
{};

struct PrefabInstanceComponent
{
    static constexpr const char* COMPONENT_NAME = "PrefabInstanceComponent";

    StringID prefabId;
    bool bMasterPrefab{false};

    static void Serialize(const PrefabInstanceComponent& comp, Engine::TextWriter& w);
    static void Deserialize(PrefabInstanceComponent& comp, const Engine::TextReader& r);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_COMMON_COMPONENTS_H
