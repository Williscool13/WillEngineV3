//
// Created by William on 2026-02-08.
//

#include "core_systems.h"

#include "core/input/input_frame.h"
#include "engine/engine_api.h"

namespace Game
{
void FunctionKeySystem(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->inputFrame->GetKey(Key::F10).down) {
        // state->
    }
}
} // Game