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
 * @brief Draws the "Debug View" window: render debug toggles, lighting mode, shader pipeline
 *        overrides, ReSTIR DI settings, hotkey reference, and debug-view target buttons.
 */
void DrawDebugViewWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * @brief Draws the "Lighting" window: ReSTIR DI settings, the denoiser (A-Trous / A-SVGF /
 *        RELAX), and ambient occlusion. Groups the stochastic-lighting controls in one place.
 */
void DrawLightingWindow(Engine::EngineState* state);

/**
 * @brief Draws an editor for a single post-process configuration as a set of collapsible
 *        sections (tonemapping, exposure, bloom, ... dither). Operates solely on the passed
 *        config so it can back both the global settings window and future per-volume overrides.
 * @param pp Post-process configuration edited in place.
 * @return True if any value changed this frame.
 * @note When drawing more than one config in the same window, wrap each call in
 *       ImGui::PushID/PopID to keep widget IDs unique.
 */
bool DrawPostProcessConfig(Core::PostProcessConfiguration& pp);

/**
 * @brief Draws the "Post-Processing" window: global render settings (GTAO, anti-aliasing,
 *        denoiser) plus the per-config image effects via DrawPostProcessConfig.
 */
void DrawPostProcessingWindow(Engine::EngineState* state);
}

#endif //WILL_ENGINE_GRAPHICS_SETTINGS_H
