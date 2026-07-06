//
// Created by William on 2026-02-08.
//

#include "core_systems.h"

#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "game/input/game_actions.h"

namespace Game
{
void FunctionKeyUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->input.GetActionState(Actions::ACTION_SCREENSHOT).pressed) {
        state->bWantsScreenshot |= true;
    }
}

void FunctionKeyRenderUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->bTakeScreenshot = state->bWantsScreenshot;
    state->bWantsScreenshot = false;
}
} // Game