//
// Created by William on 2026-08-03.
//

#ifndef WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H
#define WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H

#include <entt/entt.hpp>

#include "engine/engine_api.h"

namespace Core
{
struct ViewFamily;
}

namespace Game::Component
{
/**
 * Hand-placed axis-aligned local DDGI probe volume (10^3)
 * Entity translation = window CENTRE; the probe lattice is anchored to the window's min corner, centre - 4.5 * spacing.
 * Bounds derive as (count - 1) * spacing per axis, so only position and spacing are authored.
 */
struct LocalDDGIVolumeComponent
{
    static constexpr const char* COMPONENT_NAME = "LocalDDGIVolumeComponent";

    uint64_t volumeId{0};
    bool bEnabled{true};
    float probeSpacing{0.5f};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const LocalDDGIVolumeComponent& comp, Engine::TextWriter& w);

    static void Deserialize(LocalDDGIVolumeComponent& comp, const Engine::TextReader& r);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    /**
     * Draws the fully-owned interior (the window minus its one-cell fade band); the outer window is never drawn. Face crosses only when bCrosses (selected / editing).
     */
    static void DrawWindow(Core::ViewFamily& viewFamily, const Vec3& centre, float spacing, const Vec4& color, float lineWidth, bool bCrosses);

    /** Lattice min corner for an authored centre. */
    static Vec3 WindowCorner(const Vec3& centre, float spacing);
};
}

#endif //WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H
