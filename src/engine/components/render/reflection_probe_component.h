//
// Created by William on 2026-07-22.
//

#ifndef WILL_ENGINE_REFLECTION_PROBE_COMPONENT_H
#define WILL_ENGINE_REFLECTION_PROBE_COMPONENT_H

#include <entt/entt.hpp>

#include "engine/engine_api.h"
#include "engine/asset_manager_types.h"
#include "engine/core/environment_map_id.h"
#include "engine/reflection/reflection.h"

namespace Core
{
struct ViewFamily;
}

namespace Engine
{
struct ProbeBakeSnapshot;
}

namespace Engine::Component
{
struct WorldTransformComponent;

struct ReflectionProbeComponent
{
    static constexpr const char* COMPONENT_NAME = "ReflectionProbeComponent";

    enum class Shape : uint32_t
    {
        Box = 0,
        Sphere = 1,
    };

    enum class Resolution : uint32_t
    {
        Res128 = 0,
        Res256 = 1,
    };

    enum class ContentSource : uint8_t
    {
        None = 0,
        StandIn = 1,
        Baked = 2,
    };

    uint64_t probeId{0};
    bool bEnabled{true};
    Shape shape{Shape::Box};
    float fadeMargin{0.5f};
    Vec3 captureOffset{0.0f, 0.0f, 0.0f};
    bool bParallax{true};
    Resolution resolution{Resolution::Res256};
    Engine::EnvironmentMapID standInEnvMap{};
    float standInIntensity{65536.0f}; // stand-in env map texel value to nits

    // Runtime-only
    // Resident cubemap for the current content, recorded on load resolve.
    Engine::CubemapHandle contentHandle{Engine::CubemapHandle::INVALID};
    ContentSource contentSource{ContentSource::None};
    bool bBakeRequested{false};

    WILL_REFLECT(ReflectionProbeComponent,
        WILL_FIELD(probeId),
        WILL_FIELD(bEnabled),
        WILL_FIELD(shape),
        WILL_FIELD(fadeMargin, .min = 0.0f, .max = 10.0f, .speed = 0.02f),
        WILL_FIELD(captureOffset, .speed = 0.05f),
        WILL_FIELD(bParallax),
        WILL_FIELD(resolution),
        WILL_FIELD(standInEnvMap),
        WILL_FIELD(standInIntensity, .min = 0.0f, .max = 1.0e9f))

    static void OnEditCommit(entt::registry& registry, entt::entity entity);

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);

    /** True when the live world transform, captureOffset, or resolution no longer matches the .wprobe bake snapshot; shading uses the snapshot until rebaked. */
    static bool IsBakeStale(const WorldTransformComponent& world, const ReflectionProbeComponent& comp, const Engine::ProbeBakeSnapshot& snapshot, uint32_t bakedResolutionPx);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);

    static void DeferredConstruct(entt::registry& registry, entt::entity entity);
};

/** Stand-in cubemap load requested; ReflectionProbePendingKickoff kicks the cubemap load. */
struct ReflectionProbeLoadPendingTag
{};

/** Cubemap load in flight; ReflectionProbeLoadResolve records the resident handle once it finishes. */
struct ReflectionProbeLoadingTag
{};

/** Transient bake-time hide: tagged entities drop out of raster, TLAS, and light gather for the capture. */
struct ProbeBakeHiddenTag
{
    static constexpr const char* COMPONENT_NAME = "ProbeBakeHiddenTag";
};

/** Transient bake-time hide of the emissive proxy surface only. Light still contributes to GI, but not to DI in probe bake.*/
struct ProbeBakeProxyHiddenTag
{};
}

#endif //WILL_ENGINE_REFLECTION_PROBE_COMPONENT_H
