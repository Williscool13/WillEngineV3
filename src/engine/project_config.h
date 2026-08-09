//
// Created by William on 2026-04-22.
//

#ifndef WILL_ENGINE_PROJECT_CONFIG_H
#define WILL_ENGINE_PROJECT_CONFIG_H

#include "core/containers/inline_string.h"
#include "core/types/math.h"
#include "render/interface/render_interface.h"

namespace Engine
{
static constexpr int MAX_CAMERA_PRESETS = 8;

struct CameraPreset
{
    bool bSet{false};
    Vec3 translation{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
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
    Core::InlineString<256> defaultScene{};

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
 * @return Returns true on success.
 */
bool WriteProjectConfig(const ProjectConfig& config);
} // Engine

#endif //WILL_ENGINE_PROJECT_CONFIG_H
