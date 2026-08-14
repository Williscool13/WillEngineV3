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
    Engine::Profiles::LightingProfileBundle bundle = Engine::Profiles::CaptureLightingProfile(*state);
    if (Engine::Profiles::LoadLightingProfile(name, bundle)) {
        Engine::Profiles::ApplyLightingProfile(*state, bundle);
        state->projectConfig.activeLightingProfile = Core::InlineString<64>(name);
        Engine::WriteProjectConfig(state->projectConfig, state->allocator);
    }
}

void FunctionKeyUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->input.GetActionState(Actions::ACTION_SCREENSHOT).pressed) {
        state->requests.bWantsScreenshot |= true;
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
    frameBuffer->bTakeScreenshot = state->requests.bWantsScreenshot;
    state->requests.bWantsScreenshot = false;
}
} // Game