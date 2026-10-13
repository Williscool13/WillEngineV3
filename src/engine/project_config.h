//
// Created by William on 2026-04-22.
//

#ifndef WILL_ENGINE_PROJECT_CONFIG_H
#define WILL_ENGINE_PROJECT_CONFIG_H

#include "core/containers/inline_string.h"
#include "core/string_id.h"
#include "core/types/math.h"
#include "render/interface/render_interface.h"

namespace Core
{
class TlsfAllocator;
}

namespace Engine
{
static constexpr int MAX_CAMERA_PRESETS = 9;
static constexpr int MAX_SCENE_SLOTS = 9;

enum class LightDebugDrawMode : uint8_t { None, Selected, All };

/** Editor object types with viewport sprites and debug draws. */
enum EditorObjectBits : uint32_t
{
    EDITOR_OBJECT_AREA_LIGHT = 1u << 0,
    EDITOR_OBJECT_SPHERE_LIGHT = 1u << 1,
    EDITOR_OBJECT_DIRECTIONAL_LIGHT = 1u << 2,
    EDITOR_OBJECT_REFLECTION_PROBE = 1u << 3,
    EDITOR_OBJECT_DDGI_VOLUME = 1u << 4,
    EDITOR_OBJECT_POST_PROCESS_VOLUME = 1u << 5,
    EDITOR_OBJECT_ALL = (1u << 6) - 1u,
};

/** Physics bodies drawn: the union of every set bit. */
enum PhysicsDebugBits : uint32_t
{
    PHYSICS_DEBUG_SENSORS = 1u << 0,
    PHYSICS_DEBUG_TAGGED = 1u << 1,
    PHYSICS_DEBUG_SELECTED = 1u << 2,
    PHYSICS_DEBUG_ALL_BODIES = 1u << 3,
};

struct CameraPreset
{
    bool bSet{false};
    Vec3 translation{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct SceneSlot
{
    StringID sceneId{0};
};

/** Probe bake pipeline settings; bake-owned, deliberately outside every lighting profile. */
struct ProbeBakeSettings
{
    int32_t settleFrames{240};
    int32_t captureSize{1024};
    bool bAutoConverge{true};
    bool bAutoFreeze{true};
    bool bGroundTruth{false};
    int32_t groundTruthSpp{8};
};

struct ProjectConfig
{
    StringID defaultScene{};

    bool bLimitFps{false};
    int32_t frameLimitTarget{60};

    bool bAutoSaveProjectConfig{false};
    bool bAutoSaveLighting{false};
    bool bAutoSavePostProcess{false};

    /** Names of the lighting/post-process profiles to load on startup; their contents live in the .wprofile files, not here. */
    Core::InlineString<> activeLightingProfile{};
    Core::InlineString<> activePostProcessProfile{};
    Core::InlineString<> activeInputProfile{};

    /** Anti-aliasing is project-level (not bundled in any profile). */
    Core::AntiAliasingConfiguration aaConfig{};

    float resolutionScale{1.0f};

    /** World-space wireframe line widths, one field per debug-draw category. */
    float reflectionProbeLineWidth{0.02f};

    uint32_t editorSpriteMask{EDITOR_OBJECT_ALL};
    uint32_t editorDebugDrawMask{EDITOR_OBJECT_ALL};
    LightDebugDrawMode editorDebugDrawMode{LightDebugDrawMode::Selected};
    uint32_t editorPhysicsDebugMask{PHYSICS_DEBUG_SENSORS};

    /** Camera projection. Aspect is viewport-derived, far is infinite (reverse-Z). */
    float gameCameraFovDegrees{70.0f};
    float gameCameraNearPlane{0.1f};
    float editorCameraFovDegrees{70.0f};
    float editorCameraNearPlane{0.1f};

    // Optional
    bool gameCameraLockAspect{false};
    Vec2 gameCameraAspect{16.0f, 9.0f};

    /** Editor-camera transform bookmarks; shift-click a preset button to save, click to jump. */
    CameraPreset cameraPresets[MAX_CAMERA_PRESETS]{};

    SceneSlot sceneSlots[MAX_SCENE_SLOTS]{};

    ProbeBakeSettings probeBake{};

    [[nodiscard]] float ResolvedGameAspect(float viewportAspect) const
    {
        if (!gameCameraLockAspect) { return viewportAspect; }
        return gameCameraAspect.y > 0.0f ? gameCameraAspect.x / gameCameraAspect.y : viewportAspect;
    }
};

/**
 * Reads project.wconfig from the project root (parent of assets/).
 * @return a zeroed config if the file doesn't exist or fails to parse.
 */
ProjectConfig ReadProjectConfig();

/**
 * Writes project.wconfig to the project root (parent of assets/).
 * @param config
 * @param alloc backs the transient text buffer for the save
 * @return Returns true on success.
 */
bool WriteProjectConfig(const ProjectConfig& config, Core::TlsfAllocator* alloc);
} // Engine

#endif //WILL_ENGINE_PROJECT_CONFIG_H
