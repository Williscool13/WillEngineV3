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

#include "game/component-registry/component_registry.h"
#include "core/containers/inline_vector.h"
#include "render/interface/render_interface.h"
#include "physics/physics_config.h"
#include "resources/scene/scene.h"

namespace Core
{
struct TimeFrame;
struct InputFrame;
struct EngineContext;
}

struct ResolvedCollisionEvent
{
    entt::entity e1{entt::null};
    entt::entity e2{entt::null};
    glm::vec3 worldNormal{};
    glm::vec3 contactPoint{};
    float penetrationDepth{0.0f};
};

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
    bool bGameCursorCaptured{false};

    const Core::InputFrame* inputFrame{nullptr};
    const Core::TimeFrame* timeFrame{nullptr};
    std::mt19937_64 rng{std::random_device{}()};

    entt::registry registry;
    std::unordered_map<StringID, entt::entity> stableIdToEntityMap;
    Game::ComponentRegistry componentRegistry{};

    // Asset Loading
    bool bPendingModelResolve{false};
    StaticModelHandle portalPlaneHandle{StaticModelHandle::INVALID};

    // Physics
    float physicsDeltaTimeAccumulator = 0.0f;
    float physicsInterpolationAlpha = 0.0f;
    std::map<JPH::BodyID, entt::entity> bodyToEntity;
    Core::InlineVector<ResolvedCollisionEvent, Physics::MAX_COLLISION_EVENTS> resolvedAddedEvents;
    Core::InlineVector<ResolvedCollisionEvent, Physics::MAX_COLLISION_EVENTS> resolvedPersistedEvents;
    Core::InlineVector<ResolvedCollisionEvent, Physics::MAX_COLLISION_EVENTS> resolvedRemovedEvents;
    bool bEnablePhysics = true;

    // Lighting
    Core::DirectionalLight directionalLight{};
    Core::ShadowQuality shadowQuality = Core::ShadowQuality::Ultra;
    Core::ShadowConfiguration shadowConfig;
    Core::GTAOConfiguration gtaoConfig{};
    Core::PostProcessConfiguration postProcess{};
    CubemapHandle skybox{CubemapHandle::INVALID};

    // Debug
    bool bEnablePortal{true};
    std::string debugResourceName{};
    DebugTransformationType debugTransformationType{};
    Core::DebugViewAspect debugViewAspect{};

    // If true transform imguizmo will not be drawn
    bool bCustomGizmoActive{false};
    bool bCustomGizmoActivePrev{false};

    // Editor
    //  Gizmo
    ImGuizmo::OPERATION currentGizmoOperation{ImGuizmo::TRANSLATE};
    ImGuizmo::MODE currentGizmoMode{ImGuizmo::WORLD};
    bool bUniformScaleMode{true};
    bool bSnapEnabled{true};
    bool bSnapWorldGrid{true};
    float snapTranslation{0.25f};
    float snapRotation{15.0f};
    float snapScale{0.1f};
    //  Physics Debug
    enum class PhysicsDebugMode : uint8_t { Off, SensorOnly, SensorAndTag, On };
    PhysicsDebugMode physicsDebugMode{PhysicsDebugMode::SensorOnly};
    //  Scene
    struct RuntimeSceneMetadata
    {
        StringID sceneId;
        uint64_t nextSortOrder{100};
    };
    std::vector<RuntimeSceneMetadata> loadedScenes{};
    std::vector<StringID> modifiedScenes{};
    bool bAutoSave{false};
    float autoSaveInterval{60.0f};
    float autoSaveTimer{0.0f};
    //  PIE
    std::vector<Scene> pieSnapshot{};
    glm::vec3 pieCameraTranslation{};
    glm::quat pieCameraRotation{1.0f, 0.0f, 0.0f, 0.0f};
    //  Entity selection
    std::vector<entt::entity> selectedEntities{};
    std::vector<entt::entity> prevSelectedEntities{};
    bool bWantCopyEntities{false};
    bool bWantDeleteEntities{false};
    //  ImGui textures
    EditorTextureResidency texResidency{};


    // Gameplay
    StringID currentCheckpointId{};
    int32_t currentCheckpointPriority{INT32_MIN};

    // Scene stuff
    StringID currentSceneId{0};
    std::string currentSceneName{};
};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
