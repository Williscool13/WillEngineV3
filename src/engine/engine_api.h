//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_API_H
#define WILL_ENGINE_ENGINE_API_H

#include <vector>

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include "game/components/component_registry.h"
#include "core/include/render_interface.h"
#include "resources/scene/scene.h"

namespace Core
{
struct TimeFrame;
struct InputFrame;
struct EngineContext;
}

namespace Engine
{
struct Texture;
struct Sampler;

struct EditorTextureResidency
{
    struct Entry
    {
        Texture* texture{nullptr};
        uint64_t descSet{0};
    };

    Sampler* sampler{nullptr};
    std::unordered_map<TextureID, Entry> entries;
    std::vector<std::pair<Entry, uint64_t>> pendingRemoval; // {descSet, freeOnFrame}

    void Tick(Core::EngineContext* ctx);
    void Acquire(TextureID id, Core::EngineContext* ctx);
    uint64_t GetDescSet(TextureID id, Core::EngineContext* ctx);
    void Release(TextureID id, Core::EngineContext* ctx);
    void ReleaseAll(Core::EngineContext* ctx);
};

struct GameState
{
    bool bIsPlaying{false};

    const Core::InputFrame* inputFrame{nullptr};
    const Core::TimeFrame* timeFrame{nullptr};
    std::mt19937_64 rng{std::random_device{}()};

    entt::registry registry;
    std::unordered_map<StringID, entt::entity> stableIdToEntityMap;
    Game::ComponentRegistry componentRegistry{};

    // Physics
    float physicsDeltaTimeAccumulator = 0.0f;
    float physicsInterpolationAlpha = 0.0f;
    std::map<JPH::BodyID, entt::entity> bodyToEntity;
    bool bEnablePhysics = true;

    // Shadows
    Core::DirectionalLight directionalLight{};
    Core::ShadowQuality shadowQuality = Core::ShadowQuality::Ultra;
    Core::ShadowConfiguration shadowConfig;

    Core::GTAOConfiguration gtaoConfig{};

    // Post-Process
    Core::PostProcessConfiguration postProcess{};

    // Asset Loading
    bool bPendingModelResolve{false};

    // Loaded models debug
    StaticModelHandle portalPlaneHandle{StaticModelHandle::INVALID};

    // Debug
    bool bEnablePortal{true};
    std::string debugResourceName{};
    DebugTransformationType debugTransformationType{};
    Core::DebugViewAspect debugViewAspect{};

    // If true transform imguizmo will not be drawn
    bool bCustomGizmoActive{false};
    bool bCustomGizmoActivePrev{false};

    ImGuizmo::OPERATION currentGizmoOperation{ImGuizmo::TRANSLATE};
    ImGuizmo::MODE currentGizmoMode{ImGuizmo::WORLD};
    bool bUniformScaleMode{true};

    // Gizmo snapping
    bool bSnapEnabled{false};
    float snapTranslation{0.5f};
    float snapRotation{15.0f};
    float snapScale{0.1f};

    // Scene stuff
    StringID currentSceneId{"main_scene"_sid};
    std::string currentSceneName{"Main Scene"};

    std::vector<StringID> loadedScenes{};
    std::vector<Scene> pieSnapshot{};

    std::vector<entt::entity> selectedEntities{};
    std::vector<entt::entity> prevSelectedEntities{};
    bool bWantCopyEntities{false};
    bool bWantDeleteEntities{false};

    EditorTextureResidency texResidency{};
};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
