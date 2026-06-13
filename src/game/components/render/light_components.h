//
// Created by William on 2026-05-23.
//

#ifndef WILL_ENGINE_LIGHT_COMPONENTS_H
#define WILL_ENGINE_LIGHT_COMPONENTS_H

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/engine_api.h"

namespace Core
{
struct ViewFamily;
}

namespace Game::Component
{
struct PointLightComponent
{
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float range{10.0f};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const PointLightComponent& comp, nlohmann::json& json);

    static void Deserialize(PointLightComponent& comp, const nlohmann::json& json);
};

struct AreaLightComponent
{
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float halfWidth{1.0f};
    float halfHeight{1.0f};
    float range{10.0f};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const AreaLightComponent& comp, nlohmann::json& json);

    static void Deserialize(AreaLightComponent& comp, const nlohmann::json& json);
};

struct DirectionalLightComponent
{
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{2.0f};
    int32_t priority{0};
    float angularRadiusDegrees{1.0f}; // sun-disk half-angle for soft shadows; 0 = hard

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const DirectionalLightComponent& comp, nlohmann::json& json);

    static void Deserialize(DirectionalLightComponent& comp, const nlohmann::json& json);
};
}

#endif //WILL_ENGINE_LIGHT_COMPONENTS_H
