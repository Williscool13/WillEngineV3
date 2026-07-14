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

static void AddDefaultAllContexts(Engine::InputState& input, Engine::ActionHandle action, Engine::BindingSource source)
{
    AddDefault(input, action, Engine::InputContext::Editor, source);
    AddDefault(input, action, Engine::InputContext::Menu, source);
    AddDefault(input, action, Engine::InputContext::Gameplay, source);
}

static void AddDefaultStickAllContexts(Engine::InputState& input, Engine::ActionHandle action, Engine::AnalogStick2D stick)
{
    AddDefaultStick(input, action, Engine::InputContext::Editor, stick);
    AddDefaultStick(input, action, Engine::InputContext::Menu, stick);
    AddDefaultStick(input, action, Engine::InputContext::Gameplay, stick);
}

static void AddDefaultEditorAndMenu(Engine::InputState& input, Engine::ActionHandle action, Engine::BindingSource source)
{
    AddDefault(input, action, Engine::InputContext::Editor, source);
    AddDefault(input, action, Engine::InputContext::Menu, source);
}

static void AddDefaultCompositeEditorAndMenu(Engine::InputState& input, Engine::ActionHandle action, Engine::AxisComposite2D composite)
{
    AddDefaultComposite2D(input, action, Engine::InputContext::Editor, composite);
    AddDefaultComposite2D(input, action, Engine::InputContext::Menu, composite);
}

static void AddDefaultStickEditorAndMenu(Engine::InputState& input, Engine::ActionHandle action, Engine::AnalogStick2D stick)
{
    AddDefaultStick(input, action, Engine::InputContext::Editor, stick);
    AddDefaultStick(input, action, Engine::InputContext::Menu, stick);
}

void RegisterInputActions(Engine::InputState& input)
{
    input.defaultBindings.Clear();
    input.actionIndex.Clear();

    // Globals
    AddDefaultAllContexts(input, Actions::ACTION_SCREENSHOT, Engine::BindingSource::FromKey(Key::F10));
    AddDefaultAllContexts(input, Actions::ACTION_ESCAPE, Engine::BindingSource::FromKey(Key::ESCAPE));
    AddDefaultAllContexts(input, Actions::ACTION_DEBUG_PLAY_MUSIC, Engine::BindingSource::FromKey(Key::M));
    AddDefaultAllContexts(input, Actions::ACTION_DEBUG_MUSIC_VOL_LOW, Engine::BindingSource::FromKey(Key::N));
    AddDefaultAllContexts(input, Actions::ACTION_DEBUG_MUSIC_VOL_FULL, Engine::BindingSource::FromKey(Key::B));

    AddDefaultAllContexts(input, Actions::ACTION_MODIFIER_CTRL, Engine::BindingSource::FromKey(Key::LCTRL));
    AddDefaultAllContexts(input, Actions::ACTION_MODIFIER_CTRL, Engine::BindingSource::FromKey(Key::RCTRL));
    AddDefaultAllContexts(input, Actions::ACTION_MODIFIER_SHIFT, Engine::BindingSource::FromKey(Key::LSHIFT));
    AddDefaultAllContexts(input, Actions::ACTION_MODIFIER_SHIFT, Engine::BindingSource::FromKey(Key::RSHIFT));

    AddDefaultAllContexts(input, Actions::ACTION_UI_POINTER_DOWN, Engine::BindingSource::FromMouse(MouseButton::LMB));
    AddDefaultStickAllContexts(input, Actions::ACTION_UI_SCROLL, {
        Engine::BindingSource::FromMouseWheelX(),
        Engine::BindingSource::FromMouseWheelY()
    });

    // Gameplay
    AddDefault(input, Actions::ACTION_LOAD_LIGHTING_PROFILE_RESTIR, Engine::InputContext::Gameplay, Engine::BindingSource::FromKey(Key::F1));
    AddDefault(input, Actions::ACTION_LOAD_LIGHTING_PROFILE_RESTIR, Engine::InputContext::Menu, Engine::BindingSource::FromKey(Key::F1));
    AddDefault(input, Actions::ACTION_LOAD_LIGHTING_PROFILE_STANDARD, Engine::InputContext::Gameplay, Engine::BindingSource::FromKey(Key::F2));
    AddDefault(input, Actions::ACTION_LOAD_LIGHTING_PROFILE_STANDARD, Engine::InputContext::Menu, Engine::BindingSource::FromKey(Key::F2));

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
    AddDefaultStick(input, Actions::ACTION_LOOK, Engine::InputContext::Gameplay, {
        Engine::BindingSource::FromMouseDeltaX(),
        Engine::BindingSource::FromMouseDeltaY()
    });
    AddDefaultStick(input, Actions::ACTION_LOOK_GAMEPAD, Engine::InputContext::Gameplay, {
        Engine::BindingSource::FromGamepadAxis(GamepadAxis::RIGHT_X),
        Engine::BindingSource::FromGamepadAxis(GamepadAxis::RIGHT_Y)
    });


    // Editor + "Eject out of Gameplay"
    AddDefaultEditorAndMenu(input, Actions::ACTION_VIEWPORT_SELECT, Engine::BindingSource::FromMouse(MouseButton::LMB));
    AddDefaultEditorAndMenu(input, Actions::ACTION_EDITOR_CAM_LOOK_MODIFIER, Engine::BindingSource::FromMouse(MouseButton::RMB));
    AddDefaultEditorAndMenu(input, Actions::ACTION_EDITOR_CAM_PAN_MODIFIER, Engine::BindingSource::FromMouse(MouseButton::MMB));
    AddDefaultCompositeEditorAndMenu(input, Actions::ACTION_EDITOR_CAM_MOVE, {
        Engine::BindingSource::FromKey(Key::W),
        Engine::BindingSource::FromKey(Key::S),
        Engine::BindingSource::FromKey(Key::A),
        Engine::BindingSource::FromKey(Key::D)
    });
    AddDefaultEditorAndMenu(input, Actions::ACTION_EDITOR_CAM_UP, Engine::BindingSource::FromKey(Key::SPACE));
    AddDefaultEditorAndMenu(input, Actions::ACTION_EDITOR_CAM_DOWN, Engine::BindingSource::FromKey(Key::LCTRL));
    AddDefaultStickEditorAndMenu(input, Actions::ACTION_EDITOR_CAM_MOUSE_DELTA, {
        Engine::BindingSource::FromMouseDeltaX(),
        Engine::BindingSource::FromMouseDeltaY()
    });
    AddDefaultStickEditorAndMenu(input, Actions::ACTION_EDITOR_CAM_ZOOM_SPEED, {
        Engine::BindingSource{},
        Engine::BindingSource::FromMouseWheelY()
    });

    // Editor-Only
    AddDefault(input, Actions::ACTION_GIZMO_TRANSLATE, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::W));
    AddDefault(input, Actions::ACTION_GIZMO_ROTATE, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::E));
    AddDefault(input, Actions::ACTION_GIZMO_SCALE, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::R));
    AddDefault(input, Actions::ACTION_DUPLICATE, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::W));
    AddDefault(input, Actions::ACTION_DELETE_SELECTED, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::DEL));
    AddDefault(input, Actions::ACTION_BEGIN_RENAME, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::F2));
    AddDefault(input, Actions::ACTION_FOCUS_SELECTION, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::F));
    AddDefaultAllContexts(input, Actions::ACTION_TOGGLE_CONSOLE, Engine::BindingSource::FromKey(Key::BACKTICK));
    AddDefault(input, Actions::ACTION_TOGGLE_CONSOLE, Engine::InputContext::Console, Engine::BindingSource::FromKey(Key::BACKTICK));
    AddDefault(input, Actions::ACTION_ESCAPE, Engine::InputContext::Console, Engine::BindingSource::FromKey(Key::ESCAPE));

    AddDefault(input, Actions::ACTION_DEBUG_VIEW_1, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_1));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_2, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_2));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_3, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_3));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_4, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_4));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_5, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_5));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_6, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_6));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_7, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_7));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_8, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_8));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_9, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_9));
    AddDefault(input, Actions::ACTION_DEBUG_VIEW_0, Engine::InputContext::Editor, Engine::BindingSource::FromKey(Key::NUM_0));



    Engine::ApplyDefaultBindings(input);
    input.actionStates.Resize(input.actionIndex.Size());
}
} // Game
