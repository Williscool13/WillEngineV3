//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_GRAPHICS_SETTINGS_H
#define WILL_ENGINE_GRAPHICS_SETTINGS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Core
{
struct PostProcessConfiguration;
}

namespace Engine
{
/**
 * Draws the "Debug View" window: overlays, debug-view target buttons grouped by pipeline stage (only for paths that are running), culling toggles, and the hotkey reference.
 */
void DrawDebugViewWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Draws the "Diagnostics" window: a checkbox and a fixed-height readout per zone.
 */
void DrawDiagnosticsWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Draws the "Project Config" window: frame limit, render resolution, anti-aliasing with the active mode's settings, and cameras - the global, non-profile settings persisted directly in project.wconfig.
 */
void DrawProjectConfigWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Draws the "Lighting" window: per-tab save + lighting profile picker, shading/lighting shader overrides, ReSTIR DI settings, the denoiser (A-Trous / A-SVGF / RELAX), and ambient occlusion (GTAO).
 */
void DrawLightingWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Draws the collapsible editor for one PostProcessConfiguration (tonemapping, exposure, bloom ... dither).
 * Operates solely on pp so it backs both the settings window and future per-volume overrides; wrap in ImGui::PushID/PopID when drawing more than one in the same window.
 * @param pp config edited in place.
 * @param profileState when set, groups get Save/Revert against the active post-process profile; pp must be its live config
 * @return true if any value changed this frame.
 */
bool DrawPostProcessConfig(Core::PostProcessConfiguration& pp, Engine::EngineState* profileState = nullptr);

/**
 * Draws the "Post-Processing" window: per-tab save + post-process profile picker and the per-config image effects via DrawPostProcessConfig.
 */
void DrawPostProcessingWindow(Engine::EngineState* state);
}

#endif //WILL_ENGINE_GRAPHICS_SETTINGS_H
