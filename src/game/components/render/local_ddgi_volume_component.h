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
 * Entity translation = window min corner (snapped to the nearest spacing multiple).
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
};
}

#endif //WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H
