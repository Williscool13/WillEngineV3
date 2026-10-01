//
// Created by William on 2026-05-23.
//

#ifndef WILL_ENGINE_LIGHT_COMPONENTS_H
#define WILL_ENGINE_LIGHT_COMPONENTS_H

#include <entt/entt.hpp>

#include "engine/engine_api.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/asset_manager_types.h"
#include "engine/core/environment_map_id.h"
#include "engine/resources/model/instance_store.h"
#include "engine/reflection/reflection.h"

namespace Core
{
struct ViewFamily;
}

namespace Engine::Component
{
struct TransformComponent;

struct AreaLightComponent
{
    static constexpr const char* COMPONENT_NAME = "AreaLightComponent";

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{65536.0f}; // nits
    float halfWidth{1.0f};
    float halfHeight{1.0f};
    float range{10.0f};
    float coneOuterDegrees{90.0f}; // half-angle from the normal; 90 = plain hemisphere emitter
    float coneInnerDegrees{90.0f}; // full intensity inside, smoothstep to zero at outer
    bool bDisk{false};
    bool drawEmissiveSurface{true};
    bool bExcludeFromProbeBake{false};
    bool bEnabled{true};

    /** Runtime-only stable analytic light slot. Allocated OnConstruct */
    uint32_t lightSlot{Engine::AnalyticLightStore::INVALID_SLOT};

    WILL_REFLECT(AreaLightComponent,
        WILL_FIELD(bEnabled),
        WILL_FIELD(color),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(halfWidth, .min = 0.001f, .speed = 0.01f),
        WILL_FIELD(halfHeight, .min = 0.001f, .speed = 0.01f),
        WILL_FIELD(range, .min = 0.0f, .speed = 0.1f),
        WILL_FIELD(coneOuterDegrees, .min = 0.0f, .max = 90.0f, .speed = 0.5f),
        WILL_FIELD(coneInnerDegrees, .min = 0.0f, .max = 90.0f, .speed = 0.5f),
        WILL_FIELD(bDisk),
        WILL_FIELD(drawEmissiveSurface),
        WILL_FIELD(bExcludeFromProbeBake))

    static void Sanitize(AreaLightComponent& comp);

    static void OnEditPreview(entt::registry& registry, entt::entity entity);

    static void OnEditCommit(entt::registry& registry, entt::entity entity);

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);


    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/**
 * World-space transform for an area light's emissive quad: unit XZ plane oriented to the light and scaled to its extents.
 * @param transform
 * @param light
 * @return
 */
glm::mat4 ComputeAreaLightQuadMatrix(const Transform& world, const AreaLightComponent& light);

LightInfo ComputeAreaLightInfo(const Transform& world, const AreaLightComponent& light);

struct SphereLightComponent
{
    static constexpr const char* COMPONENT_NAME = "SphereLightComponent";

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{65536.0f}; // nits
    float radius{0.5f};
    float range{10.0f};
    bool drawEmissiveSurface{true};
    bool bExcludeFromProbeBake{false};
    bool bEnabled{true};

    /** Runtime-only stable analytic light slot. Allocated OnConstruct */
    uint32_t lightSlot{Engine::AnalyticLightStore::INVALID_SLOT};

    WILL_REFLECT(SphereLightComponent,
        WILL_FIELD(bEnabled),
        WILL_FIELD(color),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(radius, .min = 0.001f, .speed = 0.01f),
        WILL_FIELD(range, .min = 0.0f, .speed = 0.1f),
        WILL_FIELD(drawEmissiveSurface),
        WILL_FIELD(bExcludeFromProbeBake))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);

    static void OnEditPreview(entt::registry& registry, entt::entity entity);

    static void OnEditCommit(entt::registry& registry, entt::entity entity);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/**
 * World-space transform for a sphere light's emissive mesh: unit sphere (r=0.5) scaled to the light's radius.
 * @param transform
 * @param light
 * @return
 */
glm::mat4 ComputeSphereLightMatrix(const Transform& world, const SphereLightComponent& light);

LightInfo ComputeSphereLightInfo(const Transform& world, const SphereLightComponent& light);

struct LightSurfaceRuntime
{
    Engine::InstanceStore::Range range{};
    Engine::ModelStore::Range modelRange{};
    Engine::MaterialID materialID{};
    uint64_t stableId{StableIdComponent::NO_ID};

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct LightSurfacePendingTag
{};

struct DirectionalLightComponent
{
    static constexpr const char* COMPONENT_NAME = "DirectionalLightComponent";

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{131072.0f}; // lux
    int32_t priority{0};
    float angularRadiusDegrees{1.0f}; // sun-disk half-angle for soft shadows; 0 = hard

    WILL_REFLECT(DirectionalLightComponent,
        WILL_FIELD(color),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(priority),
        WILL_FIELD(angularRadiusDegrees, .min = 0.0f, .max = 30.0f, .speed = 0.02f))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};

/**
 * Scene-declared sky, the only skybox source. Highest priority wins; the winner drives skyboxIndex and its intensity multiplies the profile iblIntensity. No component (or none loaded) = no skybox.
 */
struct SkyboxComponent
{
    static constexpr const char* COMPONENT_NAME = "SkyboxComponent";

    Engine::EnvironmentMapID envMap{};
    float intensity{65536.0f}; // env map texel value to nits
    int32_t priority{0};
    bool bEnabled{true};

    // Runtime-only: refcounted cubemap acquired lazily by the skybox gather.
    Engine::CubemapHandle handle{Engine::CubemapHandle::INVALID};

    WILL_REFLECT(SkyboxComponent,
        WILL_FIELD(envMap),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(priority),
        WILL_FIELD(bEnabled))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);

    static void OnEditCommit(entt::registry& registry, entt::entity entity);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_LIGHT_COMPONENTS_H
