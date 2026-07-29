//
// Created by William on 2026-05-12.
//

#ifndef WILL_ENGINE_TEXT_COMPONENT_H
#define WILL_ENGINE_TEXT_COMPONENT_H

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "core/containers/inline_string.h"
#include "engine/asset_manager_types.h"
#include "engine/core/font_id.h"
#include "engine/core/text_material_id.h"
#include "engine/engine_api.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct TextComponent
{
    static constexpr const char* COMPONENT_NAME = "TextComponent";

    Engine::FontID fontId{};
    Engine::TextMaterialID textMaterialId{};
    Core::InlineString<256> text{};
    float renderSizePx{32.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    static void Serialize(const TextComponent& comp, nlohmann::json& json);
    static void Deserialize(TextComponent& comp, const nlohmann::json& json);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

struct TextRuntime
{
    Engine::FontHandle fontHandle{Engine::FontHandle::INVALID};
};

struct TextFontPendingTag
{};

void UnloadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity);
void LoadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity);
} // Game::Component

#endif //WILL_ENGINE_TEXT_COMPONENT_H
