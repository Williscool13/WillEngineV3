//
// Created by William on 2026-06-22.
//

#ifndef WILL_ENGINE_TEXT3D_COMPONENT_H
#define WILL_ENGINE_TEXT3D_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "engine/material_manager.h"
#include "core/containers/inline_string.h"
#include "engine/asset_manager_types.h"
#include "engine/core/font_id.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/components/render_components.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
/**
 * Extruded 3D text: the font's glyph contours triangulated and extruded into solid, lit, shadow-casting geometry (unlike TextComponent's flat MSDF quads).
 * Loading is staged via the tags below; while any is present the entity is "busy" (editing blocked).
 */
struct Text3DComponent
{
    static constexpr const char* COMPONENT_NAME = "Text3DComponent";

    Engine::FontID fontId{};
    Core::InlineString<256> text{};
    float depth{0.2f};
    float flatness{0.005f};
    float tracking{0.0f};
    float scale{1.0f};
    float wrapWidth{0.0f};
    float bendRadius{0.0f};
    bool bSmoothNormals{true};
    Engine::Text3DAlign align{Engine::Text3DAlign::Left};
    Engine::Text3DAnchor anchor{Engine::Text3DAnchor::Baseline};
    Engine::MaterialID material{};
    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};

    WILL_REFLECT(Text3DComponent,
        WILL_FIELD(fontId),
        WILL_FIELD(text),
        WILL_FIELD(depth, .min = 0.001f, .max = 10.0f, .speed = 0.005f),
        WILL_FIELD(flatness, .min = 0.0005f, .max = 0.1f, .speed = 0.0005f),
        WILL_FIELD(tracking, .min = -1.0f, .max = 1.0f, .speed = 0.005f),
        WILL_FIELD(scale, .min = 0.01f, .max = 100.0f, .speed = 0.01f),
        WILL_FIELD(wrapWidth, .min = 0.0f, .max = 1000.0f, .speed = 0.05f),
        WILL_FIELD(bendRadius, .min = -1000.0f, .max = 1000.0f, .speed = 0.05f),
        WILL_FIELD(bSmoothNormals, .key = "smoothNormals"),
        WILL_FIELD(align),
        WILL_FIELD(anchor),
        WILL_FIELD(material),
        WILL_FIELD(renderOffset, .speed = 0.01f),
        WILL_FIELD(renderRotation))

    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static void OnEditPreview(entt::registry& registry, entt::entity entity);
    static void OnEditCommit(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
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
} // Engine::Component

#endif //WILL_ENGINE_TEXT3D_COMPONENT_H
