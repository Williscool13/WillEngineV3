//
// Created by William on 2026-07-04.
//

#include "input_action_registry.h"

#include <cstring>

#include "engine/input/engine_actions.h"
#include "engine/input/input_rebinding.h"

namespace Engine
{
static void EnsureActionIndex(InputState& input, ActionHandle action)
{
    if (!input.actionIndex.Find(action)) {
        input.actionIndex.Insert(action, input.actionIndex.Size());
    }
}

static ActionBinding WithOrigin(ActionBinding binding, Origin origin)
{
    binding.origin = origin;
    return binding;
}

void AddDefault(InputState& input, Origin origin, ActionHandle action, InputContext context, BindingSource source)
{
    EnsureActionIndex(input, action);
    input.defaultBindings.PushBack(WithOrigin(ActionBinding::Discrete(action, context, source), origin));
}

void AddDefaultComposite2D(InputState& input, Origin origin, ActionHandle action, InputContext context, AxisComposite2D composite)
{
    EnsureActionIndex(input, action);
    input.defaultBindings.PushBack(WithOrigin(ActionBinding::Composite(action, context, composite), origin));
}

void AddDefaultStick(InputState& input, Origin origin, ActionHandle action, InputContext context, AnalogStick2D stick)
{
    EnsureActionIndex(input, action);
    input.defaultBindings.PushBack(WithOrigin(ActionBinding::Stick(action, context, stick), origin));
}

void AddDefaultAllContexts(InputState& input, Origin origin, ActionHandle action, BindingSource source)
{
    AddDefault(input, origin, action, InputContext::Editor, source);
    AddDefault(input, origin, action, InputContext::Menu, source);
    AddDefault(input, origin, action, InputContext::Gameplay, source);
}

void AddDefaultStickAllContexts(InputState& input, Origin origin, ActionHandle action, AnalogStick2D stick)
{
    AddDefaultStick(input, origin, action, InputContext::Editor, stick);
    AddDefaultStick(input, origin, action, InputContext::Menu, stick);
    AddDefaultStick(input, origin, action, InputContext::Gameplay, stick);
}

void AddDefaultEditorAndMenu(InputState& input, Origin origin, ActionHandle action, BindingSource source)
{
    AddDefault(input, origin, action, InputContext::Editor, source);
    AddDefault(input, origin, action, InputContext::Menu, source);
}

void AddDefaultCompositeEditorAndMenu(InputState& input, Origin origin, ActionHandle action, AxisComposite2D composite)
{
    AddDefaultComposite2D(input, origin, action, InputContext::Editor, composite);
    AddDefaultComposite2D(input, origin, action, InputContext::Menu, composite);
}

void AddDefaultStickEditorAndMenu(InputState& input, Origin origin, ActionHandle action, AnalogStick2D stick)
{
    AddDefaultStick(input, origin, action, InputContext::Editor, stick);
    AddDefaultStick(input, origin, action, InputContext::Menu, stick);
}

void AddDisplayedAction(InputState& input, Origin origin, ActionHandle action, const char* name)
{
    if (input.displayedActions.IsFull()) {
        return;
    }
    input.displayedActions.PushBack({action, Core::ShortString(name), origin});
}

void FinalizeActionRegistration(InputState& input)
{
    ApplyDefaultBindings(input);
    input.actionStates.Resize(input.actionIndex.Size());
}

void RegisterEngineInputActions(InputState& input)
{
    constexpr Origin ORIGIN = Origin::Engine;

    // Globals
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCREENSHOT, BindingSource::FromKey(Key::F10));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_ESCAPE, BindingSource::FromKey(Key::ESCAPE));

    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_MODIFIER_CTRL, BindingSource::FromKey(Key::LCTRL));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_MODIFIER_CTRL, BindingSource::FromKey(Key::RCTRL));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_MODIFIER_SHIFT, BindingSource::FromKey(Key::LSHIFT));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_MODIFIER_SHIFT, BindingSource::FromKey(Key::RSHIFT));

    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_UI_POINTER_DOWN, BindingSource::FromMouse(MouseButton::LMB));
    AddDefaultStickAllContexts(input, ORIGIN, Actions::ACTION_UI_SCROLL, {
        BindingSource::FromMouseWheelX(),
        BindingSource::FromMouseWheelY()
    });
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_UI_PAGE_UP, BindingSource::FromKey(Key::PAGEUP));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_UI_PAGE_DOWN, BindingSource::FromKey(Key::PAGEDOWN));

    AddDefault(input, ORIGIN, Actions::ACTION_LOAD_LIGHTING_PROFILE_RESTIR, InputContext::Gameplay, BindingSource::FromKey(Key::F1));
    AddDefault(input, ORIGIN, Actions::ACTION_LOAD_LIGHTING_PROFILE_RESTIR, InputContext::Menu, BindingSource::FromKey(Key::F1));
    AddDefault(input, ORIGIN, Actions::ACTION_LOAD_LIGHTING_PROFILE_STANDARD, InputContext::Gameplay, BindingSource::FromKey(Key::F2));
    AddDefault(input, ORIGIN, Actions::ACTION_LOAD_LIGHTING_PROFILE_STANDARD, InputContext::Menu, BindingSource::FromKey(Key::F2));

    // Editor + "Eject out of Gameplay"
    AddDefaultEditorAndMenu(input, ORIGIN, Actions::ACTION_VIEWPORT_SELECT, BindingSource::FromMouse(MouseButton::LMB));
    AddDefaultEditorAndMenu(input, ORIGIN, Actions::ACTION_EDITOR_CAM_LOOK_MODIFIER, BindingSource::FromMouse(MouseButton::RMB));
    AddDefaultEditorAndMenu(input, ORIGIN, Actions::ACTION_EDITOR_CAM_PAN_MODIFIER, BindingSource::FromMouse(MouseButton::MMB));
    AddDefaultCompositeEditorAndMenu(input, ORIGIN, Actions::ACTION_EDITOR_CAM_MOVE, {
        BindingSource::FromKey(Key::W),
        BindingSource::FromKey(Key::S),
        BindingSource::FromKey(Key::A),
        BindingSource::FromKey(Key::D)
    });
    AddDefaultEditorAndMenu(input, ORIGIN, Actions::ACTION_EDITOR_CAM_UP, BindingSource::FromKey(Key::SPACE));
    AddDefaultEditorAndMenu(input, ORIGIN, Actions::ACTION_EDITOR_CAM_DOWN, BindingSource::FromKey(Key::LCTRL));
    AddDefaultStickEditorAndMenu(input, ORIGIN, Actions::ACTION_EDITOR_CAM_MOUSE_DELTA, {
        BindingSource::FromMouseDeltaX(),
        BindingSource::FromMouseDeltaY()
    });
    AddDefaultStickEditorAndMenu(input, ORIGIN, Actions::ACTION_EDITOR_CAM_ZOOM_SPEED, {
        BindingSource{},
        BindingSource::FromMouseWheelY()
    });

    // Editor-Only
    AddDefault(input, ORIGIN, Actions::ACTION_GIZMO_TRANSLATE, InputContext::Editor, BindingSource::FromKey(Key::W));
    AddDefault(input, ORIGIN, Actions::ACTION_GIZMO_ROTATE, InputContext::Editor, BindingSource::FromKey(Key::E));
    AddDefault(input, ORIGIN, Actions::ACTION_GIZMO_SCALE, InputContext::Editor, BindingSource::FromKey(Key::R));
    AddDefault(input, ORIGIN, Actions::ACTION_DUPLICATE, InputContext::Editor, BindingSource::FromKey(Key::W));
    AddDefault(input, ORIGIN, Actions::ACTION_DELETE_SELECTED, InputContext::Editor, BindingSource::FromKey(Key::DEL));
    AddDefault(input, ORIGIN, Actions::ACTION_BEGIN_RENAME, InputContext::Editor, BindingSource::FromKey(Key::F2));
    AddDefault(input, ORIGIN, Actions::ACTION_FOCUS_SELECTION, InputContext::Editor, BindingSource::FromKey(Key::F));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_TOGGLE_CONSOLE, BindingSource::FromKey(Key::BACKTICK));
    AddDefault(input, ORIGIN, Actions::ACTION_TOGGLE_CONSOLE, InputContext::Console, BindingSource::FromKey(Key::BACKTICK));
    AddDefault(input, ORIGIN, Actions::ACTION_ESCAPE, InputContext::Console, BindingSource::FromKey(Key::ESCAPE));
    AddDefault(input, ORIGIN, Actions::ACTION_UI_POINTER_DOWN, InputContext::Console, BindingSource::FromMouse(MouseButton::LMB));
    AddDefaultStick(input, ORIGIN, Actions::ACTION_UI_SCROLL, InputContext::Console, {
        BindingSource::FromMouseWheelX(),
        BindingSource::FromMouseWheelY()
    });
    AddDefault(input, ORIGIN, Actions::ACTION_UI_PAGE_UP, InputContext::Console, BindingSource::FromKey(Key::PAGEUP));
    AddDefault(input, ORIGIN, Actions::ACTION_UI_PAGE_DOWN, InputContext::Console, BindingSource::FromKey(Key::PAGEDOWN));
    AddDefault(input, ORIGIN, Actions::ACTION_MODIFIER_CTRL, InputContext::Console, BindingSource::FromKey(Key::LCTRL));
    AddDefault(input, ORIGIN, Actions::ACTION_MODIFIER_CTRL, InputContext::Console, BindingSource::FromKey(Key::RCTRL));
    AddDefault(input, ORIGIN, Actions::ACTION_MODIFIER_SHIFT, InputContext::Console, BindingSource::FromKey(Key::LSHIFT));
    AddDefault(input, ORIGIN, Actions::ACTION_MODIFIER_SHIFT, InputContext::Console, BindingSource::FromKey(Key::RSHIFT));
    AddDefault(input, ORIGIN, Actions::ACTION_SCREENSHOT, InputContext::Console, BindingSource::FromKey(Key::F10));

    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_1, InputContext::Editor, BindingSource::FromKey(Key::NUM_1));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_2, InputContext::Editor, BindingSource::FromKey(Key::NUM_2));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_3, InputContext::Editor, BindingSource::FromKey(Key::NUM_3));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_4, InputContext::Editor, BindingSource::FromKey(Key::NUM_4));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_5, InputContext::Editor, BindingSource::FromKey(Key::NUM_5));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_6, InputContext::Editor, BindingSource::FromKey(Key::NUM_6));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_7, InputContext::Editor, BindingSource::FromKey(Key::NUM_7));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_8, InputContext::Editor, BindingSource::FromKey(Key::NUM_8));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_9, InputContext::Editor, BindingSource::FromKey(Key::NUM_9));
    AddDefault(input, ORIGIN, Actions::ACTION_DEBUG_VIEW_0, InputContext::Editor, BindingSource::FromKey(Key::NUM_0));

    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_1, BindingSource::FromKey(Key::KP_1));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_2, BindingSource::FromKey(Key::KP_2));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_3, BindingSource::FromKey(Key::KP_3));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_4, BindingSource::FromKey(Key::KP_4));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_5, BindingSource::FromKey(Key::KP_5));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_6, BindingSource::FromKey(Key::KP_6));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_7, BindingSource::FromKey(Key::KP_7));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_8, BindingSource::FromKey(Key::KP_8));
    AddDefaultAllContexts(input, ORIGIN, Actions::ACTION_SCENE_SLOT_9, BindingSource::FromKey(Key::KP_9));

#ifdef WDEBUG
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_MODE, InputContext::Gameplay, BindingSource::FromKey(Key::F12));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_1, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_1));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_2, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_2));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_3, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_3));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_4, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_4));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_5, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_5));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_6, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_6));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_7, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_7));
    AddDefault(input, ORIGIN, Actions::ACTION_PROFILE_CAM_8, InputContext::Gameplay, BindingSource::FromKey(Key::NUM_8));
#endif

    FinalizeActionRegistration(input);
}

void ClearGameInputActions(InputState& input)
{
    for (size_t i = input.defaultBindings.Size(); i-- > 0;) {
        if (input.defaultBindings[i].origin == Origin::Game) {
            input.defaultBindings.RemoveAt(i);
        }
    }
    for (size_t i = input.displayedActions.Size(); i-- > 0;) {
        if (input.displayedActions[i].origin == Origin::Game) {
            input.displayedActions.RemoveAt(i);
        }
    }
    FinalizeActionRegistration(input);
}
} // Engine
