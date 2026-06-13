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

namespace Game
{
/**
 * Draws the "Debug View" window: render debug toggles, shader pipeline overrides, hotkey reference, and debug-view target buttons.
 */
void DrawDebugViewWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Draws the "Project Config" window: lighting mode, anti-aliasing (mode + SMAA), and TAA - the global, non-profile render settings persisted directly in project.wconfig.
 */
void DrawProjectConfigWindow(Engine::EngineState* state);

/**
 * Draws the "Lighting" window: per-tab save + lighting profile picker, ReSTIR DI settings, the denoiser (A-Trous / A-SVGF / RELAX), and ambient occlusion (GTAO).
 */
void DrawLightingWindow(Engine::EngineState* state);

/**
 * Draws the collapsible editor for one PostProcessConfiguration (tonemapping, exposure, bloom ... dither).
 * Operates solely on pp so it backs both the settings window and future per-volume overrides; wrap in ImGui::PushID/PopID when drawing more than one in the same window.
 * @param pp config edited in place.
 * @return true if any value changed this frame.
 */
bool DrawPostProcessConfig(Core::PostProcessConfiguration& pp);

/**
 * Draws the "Post-Processing" window: per-tab save + post-process profile picker and the per-config image effects via DrawPostProcessConfig.
 */
void DrawPostProcessingWindow(Engine::EngineState* state);
}

#endif //WILL_ENGINE_GRAPHICS_SETTINGS_H
