//
// Created by William on 2026-07-04.
//

#include "input_action_registry.h"

#include "game_actions.h"
#include "engine/engine_api.h"
#include "engine/input/input_rebinding.h"

namespace Game
{
static void AddDefault(Engine::InputState& input, Engine::ActionHandle action, Engine::InputContext context, Engine::BindingSource source)
{
    if (!input.actionIndex.Find(action)) {
        input.actionIndex.Insert(action, input.actionIndex.Size());
    }
    input.defaultBindings.PushBack(Engine::ActionBinding::Discrete(action, context, source));
}

static void AddDefaultComposite2D(Engine::InputState& input, Engine::ActionHandle action, Engine::InputContext context, Engine::AxisComposite2D composite)
{
    if (!input.actionIndex.Find(action)) {
        input.actionIndex.Insert(action, input.actionIndex.Size());
    }
    input.defaultBindings.PushBack(Engine::ActionBinding::Composite(action, context, composite));
}

static void AddDefaultStick(Engine::InputState& input, Engine::ActionHandle action, Engine::InputContext context, Engine::AnalogStick2D stick)
{
    if (!input.actionIndex.Find(action)) {
        input.actionIndex.Insert(action, input.actionIndex.Size());
    }
    input.defaultBindings.PushBack(Engine::ActionBinding::Stick(action, context, stick));
}

void RegisterInputActions(Engine::InputState& input)
{
    input.defaultBindings.Clear();
    input.actionIndex.Clear();

    AddDefaultComposite2D(input, Actions::ACTION_MOVE, Engine::InputContext::Gameplay, {
        Engine::BindingSource::FromKey(Key::W),
        Engine::BindingSource::FromKey(Key::S),
        Engine::BindingSource::FromKey(Key::A),
        Engine::BindingSource::FromKey(Key::D)
    });
    AddDefaultStick(input, Actions::ACTION_MOVE, Engine::InputContext::Gameplay, {
        Engine::BindingSource::FromGamepadAxis(GamepadAxis::LEFT_X),
        Engine::BindingSource::FromGamepadAxis(GamepadAxis::LEFT_Y)
    });

    AddDefault(input, Actions::ACTION_JUMP, Engine::InputContext::Gameplay, Engine::BindingSource::FromKey(Key::SPACE));
    AddDefault(input, Actions::ACTION_JUMP, Engine::InputContext::Gameplay, Engine::BindingSource::FromGamepadButton(GamepadButton::SOUTH));

    Engine::ApplyDefaultBindings(input);
    input.actionStates.Resize(input.actionIndex.Size());
}
} // Game
