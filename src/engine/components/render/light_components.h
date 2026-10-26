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

enum class LightShadowMode : uint32_t
{
    Off = 0,
    Dynamic = 1,
    Baked = 2,
};

enum class ShadowBakeResolution : uint32_t
{
    Res256 = 0,
    Res512 = 1,
    Res1024 = 2,
    Res2048 = 3,
};

inline uint32_t ShadowBakeResolutionPixels(ShadowBakeResolution resolution) { return 256u << static_cast<uint32_t>(resolution); }

struct AreaLightComponent
{
    static constexpr const char* COMPONENT_NAME = "AreaLightComponent";
    static constexpr float MIN_CONE_OUTER_DEGREES = 1.0f;

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{65536.0f}; // nits
    float halfWidth{1.0f};
    float halfHeight{1.0f};
    float range{10.0f};
    float falloffExponent{2.0f}; // 2 = inverse square, 0 = none; pivots at 1 m
    float volumetricScale{1.0f}; // fog scattering multiplier; 0 = invisible to fog
    float coneOuterDegrees{90.0f}; // half-angle from the normal; 90 = plain hemisphere emitter
    float coneInnerDegrees{90.0f}; // full intensity inside, smoothstep to zero at outer
    bool bNormalizeCone{false}; // a surface in the beam is lit as if under an open emitter, whatever the cone
    bool bDisk{false};
    bool drawEmissiveSurface{true};
    LightShadowMode shadowMode{LightShadowMode::Dynamic};
    ShadowBakeResolution shadowBakeResolution{ShadowBakeResolution::Res512};
    bool bMovingObjectShadows{true};
    uint64_t shadowId{0};
    bool bEnabled{true};

    WILL_REFLECT(AreaLightComponent,
        WILL_FIELD(bEnabled),
        WILL_FIELD(color),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(halfWidth, .min = 0.001f, .speed = 0.01f),
        WILL_FIELD(halfHeight, .min = 0.001f, .speed = 0.01f),
        WILL_FIELD(range, .min = 0.0f, .speed = 0.1f),
        WILL_FIELD(falloffExponent, .min = 0.0f, .max = 4.0f, .speed = 0.01f),
        WILL_FIELD(volumetricScale, .min = 0.0f, .max = 100.0f, .speed = 0.01f),
        WILL_FIELD(coneOuterDegrees, .min = MIN_CONE_OUTER_DEGREES, .max = 90.0f, .speed = 0.5f),
        WILL_FIELD(coneInnerDegrees, .min = 0.0f, .max = 90.0f, .speed = 0.5f),
        WILL_FIELD(bNormalizeCone),
        WILL_FIELD(bDisk),
        WILL_FIELD(drawEmissiveSurface),
        WILL_FIELD(shadowMode),
        WILL_FIELD(shadowBakeResolution),
        WILL_FIELD(bMovingObjectShadows, .key = "movingObjectShadows"),
        WILL_FIELD(shadowId))

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
    float falloffExponent{2.0f}; // 2 = inverse square, 0 = none; pivots at 1 m
    float volumetricScale{1.0f}; // fog scattering multiplier; 0 = invisible to fog
    bool drawEmissiveSurface{true};
    LightShadowMode shadowMode{LightShadowMode::Dynamic};
    ShadowBakeResolution shadowBakeResolution{ShadowBakeResolution::Res512};
    bool bMovingObjectShadows{true};
    uint64_t shadowId{0};
    bool bEnabled{true};

    WILL_REFLECT(SphereLightComponent,
        WILL_FIELD(bEnabled),
        WILL_FIELD(color),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(radius, .min = 0.001f, .speed = 0.01f),
        WILL_FIELD(range, .min = 0.0f, .speed = 0.1f),
        WILL_FIELD(falloffExponent, .min = 0.0f, .max = 4.0f, .speed = 0.01f),
        WILL_FIELD(volumetricScale, .min = 0.0f, .max = 100.0f, .speed = 0.01f),
        WILL_FIELD(drawEmissiveSurface),
        WILL_FIELD(shadowMode),
        WILL_FIELD(shadowBakeResolution),
        WILL_FIELD(bMovingObjectShadows, .key = "movingObjectShadows"),
        WILL_FIELD(shadowId))

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

/** Emplaced by the light's OnConstruct; the slot is allocated at command playback. bakedShadow is resident while the bake matches the light. */
struct AreaLightRuntime
{
    uint32_t lightSlot{Engine::AnalyticLightStore::INVALID_SLOT};
    Engine::TextureArrayHandle bakedShadow{Engine::TextureArrayHandle::INVALID};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct SphereLightRuntime
{
    uint32_t lightSlot{Engine::AnalyticLightStore::INVALID_SLOT};
    Engine::TextureArrayHandle bakedShadow{Engine::TextureArrayHandle::INVALID};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/** The analytic light slot of an area or sphere light, or INVALID_SLOT. */
inline uint32_t FindLightSlot(const entt::registry& registry, entt::entity entity)
{
    if (const auto* area = registry.try_get<AreaLightRuntime>(entity)) { return area->lightSlot; }
    if (const auto* sphere = registry.try_get<SphereLightRuntime>(entity)) { return sphere->lightSlot; }
    return Engine::AnalyticLightStore::INVALID_SLOT;
}

struct LightSurfaceRuntime
{
    Engine::InstanceStore::Range range{};
    Engine::ModelStore::Range modelRange{};
    Engine::MaterialID materialID{};
    uint64_t stableId{StableIdComponent::NO_ID};

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct LightSurfaceRequest
{};

struct DirectionalLightComponent
{
    static constexpr const char* COMPONENT_NAME = "DirectionalLightComponent";

    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{131072.0f}; // lux
    int32_t priority{0};
    float angularRadiusDegrees{1.0f}; // sun-disk half-angle for soft shadows; 0 = hard
    float volumetricScale{1.0f}; // fog scattering multiplier; 0 = invisible to fog

    WILL_REFLECT(DirectionalLightComponent,
        WILL_FIELD(color),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(priority),
        WILL_FIELD(angularRadiusDegrees, .min = 0.0f, .max = 30.0f, .speed = 0.02f),
        WILL_FIELD(volumetricScale, .min = 0.0f, .max = 100.0f, .speed = 0.01f))

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

    WILL_REFLECT(SkyboxComponent,
        WILL_FIELD(envMap),
        WILL_FIELD(intensity, .min = 0.0f, .speed = 100.0f),
        WILL_FIELD(priority),
        WILL_FIELD(bEnabled))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);

    static void OnEditCommit(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/** The refcounted cubemap of envMap, acquired lazily by ResolveSkyboxCubemaps. */
struct SkyboxRuntime
{
    Engine::CubemapHandle handle{Engine::CubemapHandle::INVALID};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/**
 * Scene-declared global height fog. Highest priority wins; no enabled component = fog fully off.
 */
struct VolumetricFogComponent
{
    static constexpr const char* COMPONENT_NAME = "VolumetricFogComponent";

    Vec3 albedo{1.0f, 1.0f, 1.0f};
    float density{0.005f}; // extinction per metre at baseHeight
    float heightFalloff{0.1f}; // per metre above baseHeight
    float baseHeight{0.0f};
    float anisotropy{0.2f};
    float ambientScale{1.0f};
    float maxDistance{128.0f};
    int32_t priority{0};
    bool bEnabled{true};

    WILL_REFLECT(VolumetricFogComponent,
        WILL_FIELD(albedo),
        WILL_FIELD(density, .min = 0.0f, .max = 10.0f, .speed = 0.001f),
        WILL_FIELD(heightFalloff, .min = 0.0f, .max = 10.0f, .speed = 0.001f),
        WILL_FIELD(baseHeight, .speed = 0.1f),
        WILL_FIELD(anisotropy, .min = -0.95f, .max = 0.95f, .speed = 0.01f),
        WILL_FIELD(ambientScale, .min = 0.0f, .max = 10.0f, .speed = 0.01f),
        WILL_FIELD(maxDistance, .min = 1.0f, .max = 1000.0f, .speed = 0.5f),
        WILL_FIELD(priority),
        WILL_FIELD(bEnabled))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};

/**
 * Local participating medium sized by the entity scale (half extents, like post process volumes). Absorption darkens what is seen through it per pixel;
 * scattering is lit haze in the froxel grid. Densities ramp linearly from bottomScale to topScale along local Y.
 */
struct FogVolumeComponent
{
    static constexpr const char* COMPONENT_NAME = "FogVolumeComponent";

    enum class Shape : uint32_t
    {
        Box = 0,
        Cylinder = 1,
    };

    Shape shape{Shape::Box};
    float absorption{0.0f}; // per metre
    float scattering{0.05f}; // per metre
    Vec3 albedo{1.0f, 1.0f, 1.0f};
    float edgeFade{1.0f}; // metres, sides
    float heightFade{1.0f}; // metres, top and bottom
    float bottomScale{1.0f};
    float topScale{1.0f};
    float ambientScale{1.0f};
    float localLightScale{1.0f};
    bool bEnabled{true};

    WILL_REFLECT(FogVolumeComponent,
        WILL_FIELD(shape),
        WILL_FIELD(absorption, .min = 0.0f, .max = 100.0f, .speed = 0.005f),
        WILL_FIELD(scattering, .min = 0.0f, .max = 100.0f, .speed = 0.001f),
        WILL_FIELD(albedo),
        WILL_FIELD(edgeFade, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(heightFade, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(bottomScale, .min = 0.0f, .speed = 0.01f),
        WILL_FIELD(topScale, .min = 0.0f, .speed = 0.01f),
        WILL_FIELD(ambientScale, .min = 0.0f, .max = 10.0f, .speed = 0.01f),
        WILL_FIELD(localLightScale, .min = 0.0f, .max = 10.0f, .speed = 0.01f),
        WILL_FIELD(bEnabled))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);

    static void DrawBounds(Core::ViewFamily& viewFamily, const FogVolumeComponent& volume, const Vec3& center, const Quat& rotation, const Vec3& scale, float lineWidth, bool bSelected);
};
}

#endif //WILL_ENGINE_LIGHT_COMPONENTS_H
