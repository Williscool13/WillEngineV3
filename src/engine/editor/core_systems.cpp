//
// Created by William on 2026-02-08.
//

#include "core_systems.h"

#include "engine/input/input_frame.h"
#include "engine/engine_api.h"
#include "engine/profiles/profile_library.h"
#include "engine/project_config.h"
#include "engine/input/engine_actions.h"
#include "engine/systems/scene_system.h"

namespace Engine
{
static const Engine::ActionHandle SCENE_SLOT_ACTIONS[Engine::MAX_SCENE_SLOTS] = {
    Actions::ACTION_SCENE_SLOT_1, Actions::ACTION_SCENE_SLOT_2, Actions::ACTION_SCENE_SLOT_3,
    Actions::ACTION_SCENE_SLOT_4, Actions::ACTION_SCENE_SLOT_5, Actions::ACTION_SCENE_SLOT_6,
    Actions::ACTION_SCENE_SLOT_7, Actions::ACTION_SCENE_SLOT_8, Actions::ACTION_SCENE_SLOT_9
};

#ifdef WDEBUG
static const Engine::ActionHandle PROFILE_CAM_ACTIONS[Engine::MAX_CAMERA_PRESETS] = {
    Actions::ACTION_PROFILE_CAM_1, Actions::ACTION_PROFILE_CAM_2, Actions::ACTION_PROFILE_CAM_3, Actions::ACTION_PROFILE_CAM_4,
    Actions::ACTION_PROFILE_CAM_5, Actions::ACTION_PROFILE_CAM_6, Actions::ACTION_PROFILE_CAM_7, Actions::ACTION_PROFILE_CAM_8
};
#endif

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

#ifdef WDEBUG
    int32_t& profileSlot = state->debug.profileCameraSlot;
    if (state->input.GetActionState(Actions::ACTION_PROFILE_MODE).pressed) {
        profileSlot = profileSlot < 0 ? 0 : -1;
        state->debug.render.bDisableAsyncCompute = profileSlot >= 0;
        LOG_INFO(Engine, "Profile mode {}", profileSlot < 0 ? "off" : "on");
    }
    if (profileSlot >= 0) {
        for (int32_t i = 0; i < Engine::MAX_CAMERA_PRESETS; ++i) {
            if (state->input.GetActionState(PROFILE_CAM_ACTIONS[i]).pressed) {
                profileSlot = i;
                LOG_INFO(Engine, "Profile cam {}{}", i + 1, state->projectConfig.cameraPresets[i].bSet ? "" : " is empty");
            }
        }
    }
#endif
}

void FunctionKeyRenderUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    if (state->requests.screenshotBurstRemaining > 0) {
        state->requests.screenshotPath = Core::InlineString<512>::Format("%s_%03d.png", state->requests.screenshotBurstBase.c_str(), state->requests.screenshotBurstIndex);
        state->requests.bWantsScreenshot = true;
        --state->requests.screenshotBurstRemaining;
        ++state->requests.screenshotBurstIndex;
    }
    frameBuffer->bTakeScreenshot = state->requests.bWantsScreenshot;
    if (!state->requests.screenshotPath.IsEmpty()) {
        frameBuffer->screenshotPath = state->requests.screenshotPath;
        state->requests.screenshotPath.Clear();
    }
    state->requests.bWantsScreenshot = false;
}
} // Engine