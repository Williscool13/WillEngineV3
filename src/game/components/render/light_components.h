//
// Created by William on 2026-05-23.
//

#ifndef WILL_ENGINE_LIGHT_COMPONENTS_H
#define WILL_ENGINE_LIGHT_COMPONENTS_H

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/engine_api.h"
#include "engine/asset_manager_types.h"
#include "engine/core/environment_map_id.h"
#include "engine/resources/model/mesh_primitive_store.h"

namespace Core
{
struct ViewFamily;
}

namespace Game::Component
{
struct TransformComponent;

struct AreaLightComponent
{
    static constexpr const char* COMPONENT_NAME = "AreaLightComponent";

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float halfWidth{1.0f};
    float halfHeight{1.0f};
    float range{10.0f};
    bool drawEmissiveSurface{true};
    bool bExcludeFromProbeBake{false};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const AreaLightComponent& comp, nlohmann::json& json);

    static void Deserialize(AreaLightComponent& comp, const nlohmann::json& json);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/**
 * World-space transform for an area light's emissive quad: unit XZ plane oriented to the light and scaled to its extents.
 * @param transform
 * @param light
 * @return
 */
glm::mat4 ComputeAreaLightQuadMatrix(const TransformComponent& transform, const AreaLightComponent& light);

struct SphereLightComponent
{
    static constexpr const char* COMPONENT_NAME = "SphereLightComponent";

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float radius{0.5f};
    float range{10.0f};
    bool drawEmissiveSurface{true};
    bool bExcludeFromProbeBake{false};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const SphereLightComponent& comp, nlohmann::json& json);

    static void Deserialize(SphereLightComponent& comp, const nlohmann::json& json);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/**
 * World-space transform for a sphere light's emissive mesh: unit sphere (r=0.5) scaled to the light's radius.
 * @param transform
 * @param light
 * @return
 */
glm::mat4 ComputeSphereLightMatrix(const TransformComponent& transform, const SphereLightComponent& light);

struct LightSurfaceRuntime
{
    Engine::MeshPrimitiveStore::Range range{};
    glm::mat4 modelMatrix{1.0f};
    glm::mat4 previousMatrix{1.0f};
    Engine::MaterialID materialID{};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct LightSurfacePendingTag
{};

struct DirectionalLightComponent
{
    static constexpr const char* COMPONENT_NAME = "DirectionalLightComponent";

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{2.0f};
    int32_t priority{0};
    float angularRadiusDegrees{1.0f}; // sun-disk half-angle for soft shadows; 0 = hard

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const DirectionalLightComponent& comp, nlohmann::json& json);

    static void Deserialize(DirectionalLightComponent& comp, const nlohmann::json& json);
};

/**
 * Scene-declared sky, the only skybox source. Highest priority wins; the winner drives skyboxIndex and its intensity multiplies the profile iblIntensity. No component (or none loaded) = no skybox.
 */
struct SkyboxComponent
{
    static constexpr const char* COMPONENT_NAME = "SkyboxComponent";

    Engine::EnvironmentMapID envMap{};
    float intensity{1.0f};
    int32_t priority{0};

    // Runtime-only: refcounted cubemap acquired lazily by the skybox gather.
    Engine::CubemapHandle handle{Engine::CubemapHandle::INVALID};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const SkyboxComponent& comp, nlohmann::json& json);

    static void Deserialize(SkyboxComponent& comp, const nlohmann::json& json);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_LIGHT_COMPONENTS_H
