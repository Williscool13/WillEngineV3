//
// Created by William on 2026-06-22.
//

#ifndef WILL_ENGINE_TEXT3D_COMPONENT_H
#define WILL_ENGINE_TEXT3D_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/material_manager.h"
#include "core/containers/inline_string.h"
#include "engine/asset_manager_types.h"
#include "engine/core/font_id.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"
#include "game/components/render_components.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
/**
 * Extruded 3D text: the font's glyph contours triangulated and extruded into solid, lit, shadow-casting geometry (unlike TextComponent's flat MSDF quads).
 * Loading is staged via the tags below; while any is present the entity is "busy" (editing blocked).
 */
struct Text3DComponent
{
    Engine::FontID fontId{};
    Core::InlineString<256> text{};
    float depth{0.2f};
    float flatness{0.005f};
    float tracking{0.0f};
    float scale{1.0f};
    bool bSmoothNormals{true};
    Engine::Text3DAlign align{Engine::Text3DAlign::Left};
    Engine::Text3DAnchor anchor{Engine::Text3DAnchor::Baseline};
    Engine::MaterialID material{};
    glm::vec4 modelFlags{1.0f, 1.0f, 0.0f, 0.0f}; // x: visible, y: include in probe bake (nonzero=included, default; 0=excluded, legacy scenes store 1), z: exclude from DDGI (0=contributes, default), w: free
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    static void Serialize(const Text3DComponent& comp, nlohmann::json& json);
    static void Deserialize(Text3DComponent& comp, const nlohmann::json& json);
    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

/** Mesh needs (re)generating; the kickoff generates it (freeze-gated on the source font). */
struct Text3DGeneratePendingTag
{};

/** Mesh generation in flight; waiting to bind its material. */
struct Text3DLoadingTag
{};

/** Arms the generate pipeline; call after changing fontId. */
void LoadText3DFont(Text3DComponent& component, entt::registry& registry, entt::entity entity);

/** Releases the generated mesh and clears pending state without removing the component (for font hot-reload). */
void UnloadText3DFont(entt::registry& registry, entt::entity entity);
} // Game::Component

#endif //WILL_ENGINE_TEXT3D_COMPONENT_H
