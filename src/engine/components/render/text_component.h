//
// Created by William on 2026-05-12.
//

#ifndef WILL_ENGINE_TEXT_COMPONENT_H
#define WILL_ENGINE_TEXT_COMPONENT_H

#include <entt/entt.hpp>

#include "core/containers/inline_string.h"
#include "engine/asset_manager_types.h"
#include "engine/core/font_id.h"
#include "engine/core/text_material_id.h"
#include "engine/engine_api.h"
#include "engine/resources/model/model_store.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
struct TextComponent
{
    static constexpr const char* COMPONENT_NAME = "TextComponent";

    Engine::FontID fontId{};
    Engine::TextMaterialID textMaterialId{};
    Core::InlineString<256> text{};
    float scale{1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    Engine::Text3DAlign align{Engine::Text3DAlign::Left};
    Engine::Text3DAnchor anchor{Engine::Text3DAnchor::Baseline};
    float wrapWidth{0.0f};

    WILL_REFLECT(TextComponent,
        WILL_FIELD(fontId),
        WILL_FIELD(textMaterialId),
        WILL_FIELD(text),
        WILL_FIELD(scale, .min = 0.01f, .max = 100.0f, .speed = 0.01f),
        WILL_FIELD(color),
        WILL_FIELD(align),
        WILL_FIELD(anchor),
        WILL_FIELD(wrapWidth, .min = 0.0f, .max = 1000.0f, .speed = 0.05f))

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static void OnEditCommit(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};

struct TextRuntime
{
    static constexpr const char* COMPONENT_NAME = "TextRuntime";

    Engine::FontHandle fontHandle{Engine::FontHandle::INVALID};
    /**
     * Entity's model matrix slot in EngineState::modelStore
     */
    Engine::ModelStore::Range modelRange{};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct TextFontPendingTag
{
    static constexpr const char* COMPONENT_NAME = "TextFontPendingTag";
};

void UnloadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity);
void LoadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity);
} // Engine::Component

#endif //WILL_ENGINE_TEXT_COMPONENT_H
