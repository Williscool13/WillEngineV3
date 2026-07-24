//
// Created by William on 2026-07-22.
//

#ifndef WILL_ENGINE_REFLECTION_PROBE_COMPONENT_H
#define WILL_ENGINE_REFLECTION_PROBE_COMPONENT_H

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/engine_api.h"
#include "engine/asset_manager_types.h"
#include "engine/core/environment_map_id.h"

namespace Core
{
struct ViewFamily;
}

namespace Game::Component
{
struct ReflectionProbeComponent
{
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

    // Runtime-only
    // Resident cubemap for the current content, recorded on load resolve.
    Engine::CubemapHandle contentHandle{Engine::CubemapHandle::INVALID};
    ContentSource contentSource{ContentSource::None};
    bool bBakeRequested{false};

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);

    static void Serialize(const ReflectionProbeComponent& comp, nlohmann::json& json);

    static void Deserialize(ReflectionProbeComponent& comp, const nlohmann::json& json);

    static void OnConstruct(entt::registry& registry, entt::entity entity);

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

/** Stand-in cubemap load requested; ReflectionProbePendingKickoff kicks the cubemap load. */
struct ReflectionProbeLoadPendingTag
{};

/** Cubemap load in flight; ReflectionProbeLoadResolve records the resident handle once it finishes. */
struct ReflectionProbeLoadingTag
{};

/** Transient bake-time hide: tagged entities drop out of raster, TLAS, and light gather for the capture. Not serialized, not registered. */
struct ProbeBakeHiddenTag
{};
}

#endif //WILL_ENGINE_REFLECTION_PROBE_COMPONENT_H
