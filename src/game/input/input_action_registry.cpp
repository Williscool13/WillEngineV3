//
// Created by William on 2026-07-04.
//

#include "input_action_registry.h"

#include "engine/input/input_action_registry.h"
#include "game/input/game_actions.h"

namespace Game
{
void RegisterInputActions(Engine::InputState& input)
{
    using namespace Engine;
    constexpr Origin ORIGIN = Origin::Game;

    AddDefaultAllContexts(input, ORIGIN, Game::Actions::ACTION_DEBUG_PLAY_MUSIC, BindingSource::FromKey(Key::M));
    AddDefaultAllContexts(input, ORIGIN, Game::Actions::ACTION_DEBUG_MUSIC_VOL_LOW, BindingSource::FromKey(Key::N));
    AddDefaultAllContexts(input, ORIGIN, Game::Actions::ACTION_DEBUG_MUSIC_VOL_FULL, BindingSource::FromKey(Key::B));

    AddDefaultComposite2D(input, ORIGIN, Game::Actions::ACTION_MOVE, InputContext::Gameplay, {
        BindingSource::FromKey(Key::W),
        BindingSource::FromKey(Key::S),
        BindingSource::FromKey(Key::A),
        BindingSource::FromKey(Key::D)
    });
    AddDefaultStick(input, ORIGIN, Game::Actions::ACTION_MOVE, InputContext::Gameplay, {
        BindingSource::FromGamepadAxis(GamepadAxis::LEFT_X),
        BindingSource::FromGamepadAxis(GamepadAxis::LEFT_Y)
    });
    AddDefault(input, ORIGIN, Game::Actions::ACTION_JUMP, InputContext::Gameplay, BindingSource::FromKey(Key::SPACE));
    AddDefault(input, ORIGIN, Game::Actions::ACTION_JUMP, InputContext::Gameplay, BindingSource::FromGamepadButton(GamepadButton::SOUTH));
    AddDefaultStick(input, ORIGIN, Game::Actions::ACTION_LOOK, InputContext::Gameplay, {
        BindingSource::FromMouseDeltaX(),
        BindingSource::FromMouseDeltaY()
    });
    AddDefaultStick(input, ORIGIN, Game::Actions::ACTION_LOOK_GAMEPAD, InputContext::Gameplay, {
        BindingSource::FromGamepadAxis(GamepadAxis::RIGHT_X),
        BindingSource::FromGamepadAxis(GamepadAxis::RIGHT_Y)
    });

    AddDisplayedAction(input, ORIGIN, Game::Actions::ACTION_MOVE, "Move");
    AddDisplayedAction(input, ORIGIN, Game::Actions::ACTION_JUMP, "Jump");

    FinalizeActionRegistration(input);
}
} // Game
