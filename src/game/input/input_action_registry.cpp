//
// Created by William on 2026-07-04.
//

#include "input_action_registry.h"

#include "game_actions.h"
#include "engine/engine_api.h"

namespace Game
{
static void Add(Engine::InputState& input, Engine::ActionHandle action, Engine::InputContext context, Engine::BindingSource source)
{
    if (!input.actionIndex.Find(action)) {
        input.actionIndex.Insert(action, input.actionIndex.Size());
    }
    input.defaultBindings.PushBack(Engine::ActionBinding::Discrete(action, context, source));
}

static void AddComposite2D(Engine::InputState& input, Engine::ActionHandle action, Engine::InputContext context, Engine::AxisComposite2D composite)
{
    if (!input.actionIndex.Find(action)) {
        input.actionIndex.Insert(action, input.actionIndex.Size());
    }
    input.defaultBindings.PushBack(Engine::ActionBinding::Composite(action, context, composite));
}

void RegisterInputActions(Engine::InputState& input)
{
    input.defaultBindings.Clear();
    input.actionIndex.Clear();

    AddComposite2D(input, Actions::ACTION_MOVE, Engine::InputContext::Gameplay, {
        Engine::BindingSource::FromKey(Key::W),
        Engine::BindingSource::FromKey(Key::S),
        Engine::BindingSource::FromKey(Key::A),
        Engine::BindingSource::FromKey(Key::D)
    });
    Add(input, Actions::ACTION_JUMP, Engine::InputContext::Gameplay, Engine::BindingSource::FromKey(Key::SPACE));

    input.bindings = input.defaultBindings;
    input.actionStates.Resize(input.actionIndex.Size());
}
} // Game
