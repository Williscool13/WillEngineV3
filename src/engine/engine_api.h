//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_API_H
#define WILL_ENGINE_ENGINE_API_H


#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include "core/containers/inline_vector.h"
#include "core/containers/map.h"
#include "core/containers/vector.h"
#include "core/types/math.h"
#include "engine/core/hash.h"
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
    Vec3 worldNormal{};
    Vec3 contactPoint{};
    float penetrationDepth{0.0f};
};

namespace Engine
{
struct TextureID;
struct Texture;
struct Sampler;

struct EditorTextureResidency
{
    struct Entry
    {
        Texture* texture{nullptr};
        uint64_t descSet{0};

        uint64_t freeOnFrame{~0x0ULL};
    };

    Sampler* sampler{nullptr};
    Core::Map<TextureID, Entry> entries{};
    Core::Vector<Entry> pendingRemoval{};

    EditorTextureResidency() = default;
    explicit EditorTextureResidency(Core::TlsfAllocator* allocator);
    ~EditorTextureResidency() = default;


    void Tick(Core::EngineContext* ctx);
    void Acquire(TextureID id, Core::EngineContext* ctx);
    uint64_t GetDescSet(TextureID id, Core::EngineContext* ctx);
    void Release(TextureID id, Core::EngineContext* ctx);
    void ReleaseAll(Core::EngineContext* ctx);
};

constexpr uint32_t MAX_LOADED_SCENES = 8;

struct ComponentEditorResult {
    bool requestRemoval{false};
};

using SerializeFn = void(*)(const entt::registry&, entt::entity, nlohmann::json&);
using DeserializeFn = void(*)(entt::registry&, entt::entity, const nlohmann::json&);
using HasComponentFn = bool(*)(const entt::registry&, entt::entity);
using CanAddComponentFn = bool(*)(const entt::registry&, entt::entity);
using EmplaceDefaultFn = void(*)(entt::registry&, entt::entity);
using RemoveComponentFn = void(*)(entt::registry&, entt::entity);
using CopyComponentFn = void(*)(const entt::registry&, entt::entity, entt::registry&, entt::entity);
using DrawEditorFn = ComponentEditorResult(*)(Core::ViewFamily&, entt::registry&, entt::entity, const char*);

struct ComponentEntry
{
    StringID typeId;
    const char* name;
    SerializeFn serialize;
    DeserializeFn deserialize;
    CanAddComponentFn canAdd;

    // Type erased fns
    EmplaceDefaultFn emplaceDefault;
    RemoveComponentFn remove;
    CopyComponentFn copy;

    DrawEditorFn drawEditor;
    HasComponentFn has;
};

struct ComponentRegistry
{
    ComponentRegistry() = default;

    explicit ComponentRegistry(Core::TlsfAllocator* allocator);

    ~ComponentRegistry() = default;

    Core::Vector<ComponentEntry> registry{};
    Core::Map<StringID, size_t> registryMapping{};
};

struct EngineState
{
    EngineState() = default;

    explicit EngineState(Core::TlsfAllocator* allocator);

    ~EngineState() = default;

    bool bIsPlaying{false};
    bool bGameCursorCaptured{false};

    const Core::InputFrame* inputFrame{nullptr};
    const Core::TimeFrame* timeFrame{nullptr};
    std::mt19937_64 rng{std::random_device{}()};

    entt::registry registry;
    Core::Map<StringID, entt::entity> stableIdToEntityMap;
    ComponentRegistry componentRegistry{};

    // Asset Loading
    bool bPendingModelResolve{false};
    StaticModelHandle portalPlaneHandle{StaticModelHandle::INVALID};

    // Physics
    float physicsDeltaTimeAccumulator = 0.0f;
    float physicsInterpolationAlpha = 0.0f;
    Core::Map<JPH::BodyID, entt::entity> bodyToEntity;
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
    Core::InlineString<> debugResourceName{};
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

    Core::InlineVector<RuntimeSceneMetadata, 8> loadedScenes{};
    Core::InlineVector<StringID, 8> modifiedScenes{};
    bool bAutoSave{false};
    float autoSaveInterval{60.0f};
    float autoSaveTimer{0.0f};
    //  PIE
    Core::InlineVector<Scene, 8> pieSnapshot{};
    Vec3 pieCameraTranslation{};
    Quat pieCameraRotation{1.0f, 0.0f, 0.0f, 0.0f};
    //  Entity selection
    Core::Vector<entt::entity> selectedEntities{};
    Core::Vector<entt::entity> prevSelectedEntities{};
    bool bWantCopyEntities{false};
    bool bWantDeleteEntities{false};
    //  ImGui textures
    EditorTextureResidency texResidency{};

    // Scene stuff
    StringID currentSceneId{0};
    Core::InlineString<128> currentSceneName{};

    // Gameplay - move to GameState proper
    StringID currentCheckpointId{};
    int32_t currentCheckpointPriority{INT32_MIN};
};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
