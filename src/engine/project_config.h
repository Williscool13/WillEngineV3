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
struct ProjectConfig
{
    Core::InlineString<256> defaultScene{};

    bool bLimitFps{false};
    int32_t frameLimitTarget{60};

    bool bAutoSaveProjectConfig{false};
    bool bAutoSaveLighting{false};
    bool bAutoSavePostProcess{false};

    /** Names of the lighting/post-process profiles to load on startup; their contents live in the .wprofile files, not here. */
    Core::InlineString<64> activeLightingProfile{};
    Core::InlineString<64> activePostProcessProfile{};

    /** Anti-aliasing is project-level (not bundled in any profile). */
    Core::AntiAliasingConfiguration aaConfig{};

    float resolutionScale{1.0f};

    /** Camera projection. Aspect is viewport-derived, far is infinite (reverse-Z). */
    float gameCameraFovDegrees{70.0f};
    float gameCameraNearPlane{0.1f};
    float editorCameraFovDegrees{70.0f};
    float editorCameraNearPlane{0.1f};

    // Optional
    bool gameCameraLockAspect{false};
    Vec2 gameCameraAspect{16.0f, 9.0f};

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
