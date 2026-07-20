//
// Created by William on 2026-02-08.
//

#include "core_systems.h"

#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "engine/profiles/profile_library.h"
#include "engine/project_config.h"
#include "game/input/game_actions.h"

namespace Game
{
static void LoadLightingProfile(Engine::EngineState* state, const char* name)
{
    const bool loaded = Engine::Profiles::LoadLightingProfile(name, state->lighting.lightingMode, state->debug.restir, state->lighting.ddgi, state->lighting.reflection,
        state->lighting.gtaoConfig, state->lighting.heroShadow, state->debug.shadingShaderOverride, state->debug.lightingShaderOverride, state->lighting.iblIntensity);
    if (loaded) {
        state->projectConfig.activeLightingProfile = Core::InlineString<64>(name);
        Engine::WriteProjectConfig(state->projectConfig);
    }
}

void FunctionKeyUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->input.GetActionState(Actions::ACTION_SCREENSHOT).pressed) {
        state->bWantsScreenshot |= true;
    }
    if (state->input.GetActionState(Actions::ACTION_LOAD_LIGHTING_PROFILE_RESTIR).pressed) {
        LoadLightingProfile(state, "ReSTIR");
    }
    if (state->input.GetActionState(Actions::ACTION_LOAD_LIGHTING_PROFILE_STANDARD).pressed) {
        LoadLightingProfile(state, "Standard");
    }
}

void FunctionKeyRenderUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->bTakeScreenshot = state->bWantsScreenshot;
    state->bWantsScreenshot = false;
}
} // Game