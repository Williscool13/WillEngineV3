//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_API_H
#define WILL_ENGINE_ENGINE_API_H


#include <chrono>
#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include "clay/clay.h"
#include "core/containers/inline_vector.h"
#include "core/containers/map.h"
#include "core/containers/vector.h"
#include "core/types/math.h"
#include "engine/core/hash.h"
#include "engine/core/action_handle.h"
#include "engine/core/environment_map_id.h"
#include "engine/core/font_id.h"
#include "engine/core/model_id.h"
#include "engine/core/texture_id.h"
#include "render/interface/render_interface.h"
#include "physics/physics_config.h"
#include "resources/scene/scene.h"
#include "include/automation_config.h"
#include "project_config.h"
#include "core/input/input_frame.h"
#include "core/input/action_state.h"
#include "engine/input/input_binding.h"
#include "core/containers/arena_fixed_map.h"
#include "engine/asset_manager.h"
#include "engine/builtin_assets.h"
#include "engine/component_registry.h"
#include "engine/editor_state.h"
#include "engine/editor_texture_residency.h"
#include "engine/resources/model/instance_store.h"
#include "engine/resources/model/model_store.h"
#include "engine/resources/light/analytic_light_store.h"
#include "engine/resources/light/tri_light_store.h"

struct ResolvedCollisionEvent
{
    entt::entity e1{entt::null};
    entt::entity e2{entt::null};
    Vec3 worldNormal{};
    Vec3 contactPoint{};
    float penetrationDepth{0.0f};
};

namespace Engine
{
struct PhysicsState
{
    PhysicsState() = default;
    explicit PhysicsState(Core::TlsfAllocator* allocator);
    ~PhysicsState() = default;

    float deltaTimeAccumulator = 0.0f;
    float interpolationAlpha = 0.0f;
    bool bEnabled = true;
    Core::Map<JPH::BodyID, entt::entity> bodyToEntity;
    Core::InlineVector<ResolvedCollisionEvent, Physics::MAX_COLLISION_EVENTS> resolvedAddedEvents;
    Core::InlineVector<ResolvedCollisionEvent, Physics::MAX_COLLISION_EVENTS> resolvedPersistedEvents;
    Core::InlineVector<ResolvedCollisionEvent, Physics::MAX_COLLISION_EVENTS> resolvedRemovedEvents;
};

struct LightingState
{
    Core::LightingMode lightingMode{Core::LightingMode::Default};
    Core::GroundTruthMode groundTruthMode{Core::GroundTruthMode::None};
    bool bResetGroundTruth{false};
    int32_t groundTruthSpp{1};
    float groundTruthDofAperture{0.0f};

    Core::GTAOConfiguration gtaoConfig{};
    Core::AntiAliasingConfiguration aaConfig{};
    Core::PostProcessConfiguration postProcess{};
    Core::SIGMAParams sigmaParams{};

    Core::DDGIParams ddgi{};
    Core::ReflectionConfiguration reflection{};
    Core::ReflectionProbeConfiguration reflectionProbe{};
    float iblIntensity{1.0f};
    float indirectIntensity{1.0f};
    int32_t skyboxLOD{0};
};

struct DebugState
{
    Core::DebugRenderParams render{};

    /** Master GI freeze */
    bool bGIFreeze{false};

    bool bEnableUI{false};
    bool bEnablePortal{true};
    bool bProbePreview{false};
    bool bProbePreviewIrradiance{false};
    float probePreviewRoughness{0.0f};

    Core::ReSTIRParams restir{};
    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};
    Core::InlineString<> resourceName{};
    DebugTransformationType transformationType{};
    Core::DebugViewAspect viewAspect{};
};

struct DDGIConvergeBoost
{
    bool bActive{false};
    int32_t frame{0};
    float stashedHysteresis{0.0f};
    float stashedVisibilityHysteresis{0.0f};
    uint32_t stashedRaysPerProbe{0};
    uint32_t stashedOuterRaysPerProbe{0};
    uint32_t stashedRadianceCacheShadeInterval{0};
    uint32_t stashedRadianceCacheAccumCap{0};
};

/** One-shot requests raised during the game tick and drained by the next render prepare. */
struct FrameRequests
{
    bool bWantsScreenshot{false};
    bool bViewportClickPending{false};
    bool bRequestedQuit{false};
    Core::RenderCacheReset pendingCacheReset{Core::RenderCacheReset::None};
};

/** Drained by the per-frame asset-resolve block. */
struct AssetLoadState
{
    bool bPendingModelResolve{false};
    Core::InlineVector<ModelID, 16> pendingHotReloadModelIds{};
    Core::InlineVector<FontID, 16> pendingHotReloadFontIds{};
    Core::InlineVector<TextureID, 16> pendingHotReloadTextureIds{};
    Core::InlineVector<EnvironmentMapID, 16> pendingHotReloadEnvironmentMapIds{};

    int32_t pendingModelWaitCount{0};
    std::chrono::steady_clock::time_point modelWaitLastActivity{};

    int32_t pendingProceduralWaitCount{0};
    std::chrono::steady_clock::time_point proceduralWaitLastActivity{};
};

struct SceneState
{
    StringID currentSceneId{0};
    Core::InlineString<128> currentSceneName{};
    StringID currentCheckpointId{};
    int32_t currentCheckpointPriority{INT32_MIN};
};

struct EngineState
{
    EngineState() = default;

    explicit EngineState(Core::TlsfAllocator* allocator);

    ~EngineState() = default;

    InputContext inputContext{InputContext::Editor};
    FrameRequests requests{};

    const Core::TimeFrame* timeFrame{nullptr};

    /** The allocator this state was constructed with */
    Core::TlsfAllocator* allocator{nullptr};

    /**
     * Accumulated across game ticks since the last render-prepare; drained and reset each render frame.
     * Note: Do not use directly, timeFrame will be replaced with this in the render frame function.
     */
    Core::TimeFrame renderTimeFrame{};
    std::mt19937_64 rng{std::random_device{}()};

    entt::registry registry;
    Core::Map<StringID, entt::entity> stableIdToEntityMap;
    /** Set by hierarchy mutators (SetParent/ClearParent/SpawnModel/load); EnsureHierarchyOrder re-sorts the HierarchyComponent pool when set. */
    bool bHierarchyOrderDirty{true};
    ComponentRegistry componentRegistry{};
    Clay_Arena clayArena{};
    // todo: remove
    FontHandle uiFont{FontHandle::INVALID};

    InstanceStore instanceStore{};
    ModelStore modelStore{};
    AnalyticLightStore analyticLightStore{};
    TriLightStore triLightStore{};

    AssetLoadState assetLoad{};
    BuiltinAssets builtinAssets{};

    // Engine Features
    InputState input;

    // Gameplay
    SceneState scene{};
    Core::ScreenFadeState screenFade{};

    PhysicsState physics;
    LightingState lighting;
    EditorState editor;
    DebugState debug;
    DDGIConvergeBoost ddgiConvergeBoost;
    ProjectConfig projectConfig{};
    AutomationConfig automation{};
};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
