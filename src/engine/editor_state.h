//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_EDITOR_STATE_H
#define WILL_ENGINE_EDITOR_STATE_H

#include <cstdint>
#include <entt/entt.hpp>
#include <ImGuizmo.h>

#include "core/containers/arena_fixed_map.h"
#include "core/containers/inline_vector.h"
#include "core/containers/vector.h"
#include "core/string_id.h"
#include "core/types/math.h"
#include "engine/asset_manager.h"
#include "engine/core/material_id.h"
#include "engine/core/text_material_id.h"
#include "engine/core/texture_id.h"
#include "engine/editor_texture_residency.h"
#include "resources/scene/scene.h"

namespace Engine
{
constexpr uint32_t MAX_LOADED_SCENES = 8;

enum class PhysicsDebugMode : uint8_t { Off, SensorOnly, SensorAndTag, On, Selected };
enum class LightDebugDrawMode : uint8_t { None, Selected, All };

struct RuntimeSceneMetadata
{
    StringID sceneId;
};

enum class MaterialSort : uint8_t { Name, Usage, Id };
enum class MaterialGroup : uint8_t { Folder, Prefix, Flat };

enum MaterialFilterBits : uint32_t
{
    MATERIAL_FILTER_NONE = 0,
    MATERIAL_FILTER_IN_USE = 1u << 0,
    MATERIAL_FILTER_UNUSED = 1u << 1,
    MATERIAL_FILTER_EMISSIVE = 1u << 2,
    MATERIAL_FILTER_MASKED = 1u << 3,
    MATERIAL_FILTER_BLEND = 1u << 4,
    MATERIAL_FILTER_UNLIT = 1u << 5,
    MATERIAL_FILTER_CUSTOM_SAMPLER = 1u << 6,
};

/** One per material list; the browser and the inline selector keep independent filters. */
struct MaterialBrowserState
{
    char search[64]{};
    MaterialSort sort{MaterialSort::Name};
    MaterialGroup group{MaterialGroup::Folder};
    uint32_t filterFlags{MATERIAL_FILTER_NONE};
    bool bFilterWasActive{false};
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

    // Material browser
    MaterialBrowserState materialBrowser{};
    MaterialBrowserState materialSelector{};
    MaterialID selectedMaterial{MaterialID::INVALID};
    TextMaterialID selectedTextMaterial{TextMaterialID::INVALID};
    MaterialID renamingMaterial{MaterialID::INVALID};
    TextMaterialID renamingTextMaterial{TextMaterialID::INVALID};
    char materialRenameBuffer[128]{};
    bool bMaterialRenameRequestFocus{false};
    bool bMaterialListFocused{false};

    // ImGui textures
    EditorTextureResidency texResidency{};

    Core::ArenaFixedMap<TextureID, AssetManager::EditorTextureInfo>* textureInfoCache{nullptr};

    void ResetFrameCache()
    {
        textureInfoCache = nullptr;
    }
};
} // Engine

#endif //WILL_ENGINE_EDITOR_STATE_H
