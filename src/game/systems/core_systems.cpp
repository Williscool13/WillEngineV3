//
// Created by William on 2026-02-08.
//

#include "core_systems.h"

#include "core/input/input_frame.h"
#include "engine/engine_api.h"

namespace Game
{
void FunctionKeyUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->inputFrame->GetKey(Key::F10).pressed) {
        state->bWantsScreenshot |= true;
    }
}

void FunctionKeyRenderUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->bTakeScreenshot = state->bWantsScreenshot;
    state->bWantsScreenshot = false;
}
} // Game