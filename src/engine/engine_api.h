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
#include "project_config.h"
#include "core/input/input_frame.h"
#include "core/input/action_state.h"
#include "core/containers/arena_fixed_map.h"
#include "engine/asset_manager.h"
#include "engine/builtin_assets.h"
#include "engine/resources/model/mesh_primitive_store.h"

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


    void Tick(EngineContext* ctx);
    void Acquire(TextureID id, EngineContext* ctx);
    uint64_t GetDescSet(TextureID id, EngineContext* ctx);
    void Release(TextureID id, EngineContext* ctx);
    void ReleaseAll(EngineContext* ctx);
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

    /** Engine-managed components hidden from user-facing component lists (Add Component, filters). */
    bool hidden{false};

    /** Hide in Details inspector unless "Expose all" is enabled. */
    bool hideInInspector{false};
};

struct ComponentRegistry
{
    ComponentRegistry() = default;

    explicit ComponentRegistry(Core::TlsfAllocator* allocator);

    ~ComponentRegistry() = default;

    Core::Vector<ComponentEntry> registry{};
    Core::Map<StringID, size_t> registryMapping{};
};

enum class PhysicsDebugMode : uint8_t { Off, SensorOnly, SensorAndTag, On, Selected };
enum class LightDebugDrawMode : uint8_t { None, Selected, All };
enum class InputContext : uint8_t { Editor, Menu, Gameplay };

struct RuntimeSceneMetadata
{
    StringID sceneId;
};

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

enum class BindingSourceType : uint8_t { Key, MouseButton, GamepadButton, GamepadAxis, MouseDeltaX, MouseDeltaY, MouseWheelX, MouseWheelY };

struct BindingSource
{
    BindingSourceType type{BindingSourceType::Key};
    union
    {
        Core::Key key;
        Core::MouseButton mouseButton;
        Core::GamepadButton gamepadButton;
        Core::GamepadAxis gamepadAxis;
    };

    constexpr BindingSource();

    static constexpr BindingSource FromKey(Core::Key k)
    {
        BindingSource b;
        b.type = BindingSourceType::Key;
        b.key = k;
        return b;
    }

    static constexpr BindingSource FromMouse(Core::MouseButton m)
    {
        BindingSource b;
        b.type = BindingSourceType::MouseButton;
        b.mouseButton = m;
        return b;
    }

    static constexpr BindingSource FromGamepadButton(Core::GamepadButton b_)
    {
        BindingSource b;
        b.type = BindingSourceType::GamepadButton;
        b.gamepadButton = b_;
        return b;
    }

    static constexpr BindingSource FromGamepadAxis(Core::GamepadAxis a)
    {
        BindingSource b;
        b.type = BindingSourceType::GamepadAxis;
        b.gamepadAxis = a;
        return b;
    }

    static constexpr BindingSource FromMouseDeltaX()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseDeltaX;
        return b;
    }

    static constexpr BindingSource FromMouseDeltaY()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseDeltaY;
        return b;
    }

    static constexpr BindingSource FromMouseWheelX()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseWheelX;
        return b;
    }

    static constexpr BindingSource FromMouseWheelY()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseWheelY;
        return b;
    }

    constexpr bool operator==(const BindingSource& other) const
    {
        if (type != other.type) { return false; }
        switch (type) {
            case BindingSourceType::Key: return key == other.key;
            case BindingSourceType::MouseButton: return mouseButton == other.mouseButton;
            case BindingSourceType::GamepadButton: return gamepadButton == other.gamepadButton;
            case BindingSourceType::GamepadAxis: return gamepadAxis == other.gamepadAxis;
            case BindingSourceType::MouseDeltaX:
            case BindingSourceType::MouseDeltaY:
            case BindingSourceType::MouseWheelX:
            case BindingSourceType::MouseWheelY:
                return true;
        }
        return false;
    }
};

constexpr BindingSource::BindingSource(): key(Key::UNKNOWN) {}

struct AxisComposite2D
{
    BindingSource up;
    BindingSource down;
    BindingSource left;
    BindingSource right;
};

struct AnalogStick2D
{
    BindingSource x;
    BindingSource y;
};

enum class BindingShape : uint8_t { Discrete, Axis2DComposite, AnalogStick2D };

struct ActionBinding
{
    ActionHandle action;
    InputContext context{InputContext::Gameplay};
    BindingShape shape{BindingShape::Discrete};
    union
    {
        BindingSource source;
        AxisComposite2D composite;
        AnalogStick2D stick;
    };

    constexpr ActionBinding() : source() {}

    static constexpr ActionBinding Discrete(ActionHandle action, InputContext context, BindingSource source)
    {
        ActionBinding b;
        b.action = action;
        b.context = context;
        b.shape = BindingShape::Discrete;
        b.source = source;
        return b;
    }

    static constexpr ActionBinding Composite(ActionHandle action, InputContext context, AxisComposite2D composite)
    {
        ActionBinding b;
        b.action = action;
        b.context = context;
        b.shape = BindingShape::Axis2DComposite;
        b.composite = composite;
        return b;
    }

    static constexpr ActionBinding Stick(ActionHandle action, InputContext context, AnalogStick2D stick)
    {
        ActionBinding b;
        b.action = action;
        b.context = context;
        b.shape = BindingShape::AnalogStick2D;
        b.stick = stick;
        return b;
    }
};

struct InputState
{
    InputState() = default;
    explicit InputState(Core::TlsfAllocator* allocator);
    ~InputState() = default;

    Core::Vector<ActionBinding> bindings{};
    Core::Vector<ActionBinding> defaultBindings{};
    Core::Map<ActionHandle, size_t> actionIndex{};
    Core::Vector<Core::ActionState> actionStates{};

    bool bCaptureActive{false};
    size_t captureTargetBindingRow{~size_t{0}};
    bool bBindingsDirty{false};

    Vec2 mousePositionAbsolute{};

    [[nodiscard]] const Core::ActionState& GetActionState(ActionHandle action) const
    {
        static constexpr Core::ActionState ACTION_STATE_EMPTY{};
        const size_t* idx = actionIndex.Find(action);
        return idx ? actionStates[*idx] : ACTION_STATE_EMPTY;
    }
};

struct LightingState
{
    Core::LightingMode lightingMode{Core::LightingMode::Default};
    bool bResetGroundTruth{false};

    Core::DirectionalLight directionalLight{};
    Core::GTAOConfiguration gtaoConfig{};
    Core::AntiAliasingConfiguration aaConfig{};
    Core::PostProcessConfiguration postProcess{};
    Core::SIGMAParams sigmaParams{};
    Core::DDGIParams ddgi{};
    float iblIntensity{1.0f};
    CubemapHandle skybox{CubemapHandle::INVALID};
    int32_t skyboxLOD{0};
};

struct EditorState
{
    EditorState() = default;
    explicit EditorState(Core::TlsfAllocator* allocator);
    ~EditorState() = default;

    // Gizmo
    ImGuizmo::OPERATION currentGizmoOperation{ImGuizmo::TRANSLATE};
    ImGuizmo::MODE currentGizmoMode{ImGuizmo::WORLD};
    bool bUniformScaleMode{true};
    bool bSnapEnabled{true};
    bool bSnapWorldGrid{true};
    float snapTranslation{0.25f};
    float snapRotation{15.0f};
    float snapScale{0.1f};
    bool bExclusiveGizmoActive{false};
    bool bExclusiveGizmoActivePrev{false};
    int32_t activeDotHandleId{-1};

    PhysicsDebugMode physicsDebugMode{PhysicsDebugMode::SensorOnly};
    LightDebugDrawMode lightDebugDrawMode{LightDebugDrawMode::Selected};
    bool bShowLightSprites{true};

    // Scene management
    Core::InlineVector<RuntimeSceneMetadata, 8> loadedScenes{};
    Core::InlineVector<StringID, 8> modifiedScenes{};
    bool bAutoSave{false};
    float autoSaveInterval{60.0f};
    float autoSaveTimer{0.0f};

    // PIE
    Core::InlineVector<Scene, 8> pieSnapshot{};
    Core::InlineVector<Scene, 8> hotReloadSnapshot{};
    Vec3 pieCameraTranslation{};
    Quat pieCameraRotation{1.0f, 0.0f, 0.0f, 0.0f};

    // Entity selection
    Core::Vector<entt::entity> selectedEntities{};
    Core::Vector<entt::entity> prevSelectedEntities{};

    // Scene browser filter + selection
    char sceneBrowserSearch[64]{};
    StringID sceneBrowserComponentFilter{};
    entt::entity sceneBrowserSelectionAnchor{entt::null};
    bool sceneBrowserFilterWasActive{false};

    // Inline hierarchy rename (F2)
    entt::entity renamingEntity{entt::null};
    char renameBuffer[256]{};
    bool renameRequestFocus{false};
    bool bExposeAllComponents{false};

    // Folder selection
    Core::Vector<entt::entity> selectedFolders{};

    // ImGui textures
    EditorTextureResidency texResidency{};

    Core::ArenaFixedMap<TextureID, AssetManager::EditorTextureInfo>* textureInfoCache{nullptr};

    void ResetFrameCache()
    {
        textureInfoCache = nullptr;
    }
};

struct DebugState
{
    bool bEnableUI{false};
    bool bWireframe{false};
    bool bEnablePortal{true};
    bool bEnableShadeDispatchBucketingVisualization{false};
    bool bEnableLightingBucketingVisualization{false};
    bool bEnableGPUDebug{false};
    bool bLockGPUDebug{false};
    bool bGPUDebugTestPattern{false};
    bool bDDGIProbeDebug{false};
    Core::ReSTIRParams restir{};
    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};
    Core::InlineString<> resourceName{};
    DebugTransformationType transformationType{};
    Core::DebugViewAspect viewAspect{};
};

struct EngineState
{
    EngineState() = default;

    explicit EngineState(Core::TlsfAllocator* allocator);

    ~EngineState() = default;

    InputContext inputContext{InputContext::Editor};
    // Gathered in GameUpdate, used in PrepareRenderFrame
    bool bWantsScreenshot{false};
    bool bViewportClickPending{false};

    const Core::TimeFrame* timeFrame{nullptr};

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

    MeshPrimitiveStore meshPrimitiveStore{};

    // Asset Loading
    bool bPendingModelResolve{false};
    Core::InlineVector<ModelID, 16> pendingHotReloadModelIds{};
    Core::InlineVector<FontID, 16> pendingHotReloadFontIds{};
    Core::InlineVector<TextureID, 16> pendingHotReloadTextureIds{};
    Core::InlineVector<EnvironmentMapID, 16> pendingHotReloadEnvironmentMapIds{};

    int32_t pendingModelWaitCount{0};
    std::chrono::steady_clock::time_point modelWaitLastActivity{};

    int32_t pendingProceduralWaitCount{0};
    std::chrono::steady_clock::time_point proceduralWaitLastActivity{};
    StaticModelHandle portalPlaneHandle{StaticModelHandle::INVALID};

    BuiltinAssets builtinAssets{};

    // Gameplay
    StringID currentSceneId{0};
    Core::InlineString<128> currentSceneName{};
    StringID currentCheckpointId{};
    int32_t currentCheckpointPriority{INT32_MIN};

    PhysicsState physics;
    LightingState lighting;
    EditorState editor;
    DebugState debug;
    InputState input;
    ProjectConfig projectConfig{};
};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
