//
// Created by William on 2026-02-08.
//

#include "core_systems.h"

#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "engine/profiles/profile_library.h"
#include "engine/project_config.h"
#include "game/input/game_actions.h"
#include "game/systems/scene_system.h"

namespace Game
{
static const Engine::ActionHandle SCENE_SLOT_ACTIONS[Engine::MAX_SCENE_SLOTS] = {
    Actions::ACTION_SCENE_SLOT_1, Actions::ACTION_SCENE_SLOT_2, Actions::ACTION_SCENE_SLOT_3,
    Actions::ACTION_SCENE_SLOT_4, Actions::ACTION_SCENE_SLOT_5, Actions::ACTION_SCENE_SLOT_6,
    Actions::ACTION_SCENE_SLOT_7, Actions::ACTION_SCENE_SLOT_8, Actions::ACTION_SCENE_SLOT_9
};

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

    for (int i = 0; i < Engine::MAX_SCENE_SLOTS; ++i) {
        if (!state->input.GetActionState(SCENE_SLOT_ACTIONS[i]).pressed) { continue; }
#if WILL_EDITOR
        if (state->input.GetActionState(Actions::ACTION_MODIFIER_CTRL).down) {
            SaveSceneSlot(state, i);
            continue;
        }
#endif
        LoadSceneSlot(ctx, state, i);
    }
}

void FunctionKeyRenderUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->bTakeScreenshot = state->requests.bWantsScreenshot;
    state->requests.bWantsScreenshot = false;
}
} // Game