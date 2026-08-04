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
 * Hand-placed local DDGI probe volume: a fixed fine-spacing probe window sampled before the camera-following cascades where it covers.
 * Bounds come from the entity transform (translation = center, scale = half-extents). rotation is ignored, the probe window is world-axis aligned.
 */
struct LocalDDGIVolumeComponent
{
    static constexpr const char* COMPONENT_NAME = "LocalDDGIVolumeComponent";

    uint64_t volumeId{0};
    bool bEnabled{true};
    float probeSpacing{0.5f};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const LocalDDGIVolumeComponent& comp, nlohmann::json& json);

    static void Deserialize(LocalDDGIVolumeComponent& comp, const nlohmann::json& json);

    static void OnConstruct(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_LOCAL_DDGI_VOLUME_COMPONENT_H
