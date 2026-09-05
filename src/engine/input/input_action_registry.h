//
// Created by William on 2026-07-04.
//

#ifndef WILL_ENGINE_INPUT_ACTION_REGISTRY_H
#define WILL_ENGINE_INPUT_ACTION_REGISTRY_H

#include "engine/core/action_handle.h"
#include "engine/core/origin.h"
#include "engine/input/input_binding.h"

namespace Engine
{
void AddDefault(InputState& input, Origin origin, ActionHandle action, InputContext context, BindingSource source);
void AddDefaultComposite2D(InputState& input, Origin origin, ActionHandle action, InputContext context, AxisComposite2D composite);
void AddDefaultStick(InputState& input, Origin origin, ActionHandle action, InputContext context, AnalogStick2D stick);
void AddDefaultAllContexts(InputState& input, Origin origin, ActionHandle action, BindingSource source);
void AddDefaultStickAllContexts(InputState& input, Origin origin, ActionHandle action, AnalogStick2D stick);
void AddDefaultEditorAndMenu(InputState& input, Origin origin, ActionHandle action, BindingSource source);
void AddDefaultCompositeEditorAndMenu(InputState& input, Origin origin, ActionHandle action, AxisComposite2D composite);
void AddDefaultStickEditorAndMenu(InputState& input, Origin origin, ActionHandle action, AnalogStick2D stick);

void AddDisplayedAction(InputState& input, Origin origin, ActionHandle action, const char* name);

void FinalizeActionRegistration(InputState& input);

void RegisterEngineInputActions(InputState& input);

void ClearGameInputActions(InputState& input);
} // Engine

#endif //WILL_ENGINE_INPUT_ACTION_REGISTRY_H
