//
// Created by William on 2026-08-03.
//

#ifndef WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H
#define WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/engine_api.h"

namespace Core
{
struct ViewFamily;
}

namespace Game::Component
{
/**
 * Hand-placed axis-aligned local DDGI probe volume.
 * Entity translation = window min corner (snapped to the nearest spacing multiple).
 * Bounds derive as (count - 1) * spacing per axis.
 */
struct LocalDDGIVolumeComponent
{
    static constexpr const char* COMPONENT_NAME = "LocalDDGIVolumeComponent";

    uint64_t volumeId{0};
    bool bEnabled{true};
    float probeSpacing{0.5f};
    int32_t probeCount[3]{8, 8, 8};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const LocalDDGIVolumeComponent& comp, nlohmann::json& json);

    static void Deserialize(LocalDDGIVolumeComponent& comp, const nlohmann::json& json);

    static void OnConstruct(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H
