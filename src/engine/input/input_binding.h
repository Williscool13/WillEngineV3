//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_INPUT_BINDING_H
#define WILL_ENGINE_INPUT_BINDING_H

#include <cstdint>

#include "core/containers/inline_string.h"
#include "core/containers/map.h"
#include "core/containers/vector.h"
#include "core/input/action_state.h"
#include "core/input/input_frame.h"
#include "core/types/math.h"
#include "engine/core/action_handle.h"

namespace Engine
{
enum class InputContext : uint8_t { Editor, Menu, Gameplay, Console, ProbeBake };

enum class BindingSourceType : uint8_t { Key, MouseButton, GamepadButton, GamepadAxis, MouseDeltaX, MouseDeltaY, MouseWheelX, MouseWheelY };

struct BindingSource
{
    BindingSourceType type{BindingSourceType::Key};
    union
    {
        Core::Key key;
        Core::MouseButton mouseButton;
        Core::GamepadButton gamepadButton;
        Core::GamepadAxis gamepadAxis;
    };

    constexpr BindingSource();

    static constexpr BindingSource FromKey(Core::Key k)
    {
        BindingSource b;
        b.type = BindingSourceType::Key;
        b.key = k;
        return b;
    }

    static constexpr BindingSource FromMouse(Core::MouseButton m)
    {
        BindingSource b;
        b.type = BindingSourceType::MouseButton;
        b.mouseButton = m;
        return b;
    }

    static constexpr BindingSource FromGamepadButton(Core::GamepadButton b_)
    {
        BindingSource b;
        b.type = BindingSourceType::GamepadButton;
        b.gamepadButton = b_;
        return b;
    }

    static constexpr BindingSource FromGamepadAxis(Core::GamepadAxis a)
    {
        BindingSource b;
        b.type = BindingSourceType::GamepadAxis;
        b.gamepadAxis = a;
        return b;
    }

    static constexpr BindingSource FromMouseDeltaX()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseDeltaX;
        return b;
    }

    static constexpr BindingSource FromMouseDeltaY()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseDeltaY;
        return b;
    }

    static constexpr BindingSource FromMouseWheelX()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseWheelX;
        return b;
    }

    static constexpr BindingSource FromMouseWheelY()
    {
        BindingSource b;
        b.type = BindingSourceType::MouseWheelY;
        return b;
    }

    constexpr bool operator==(const BindingSource& other) const
    {
        if (type != other.type) { return false; }
        switch (type) {
            case BindingSourceType::Key: return key == other.key;
            case BindingSourceType::MouseButton: return mouseButton == other.mouseButton;
            case BindingSourceType::GamepadButton: return gamepadButton == other.gamepadButton;
            case BindingSourceType::GamepadAxis: return gamepadAxis == other.gamepadAxis;
            case BindingSourceType::MouseDeltaX:
            case BindingSourceType::MouseDeltaY:
            case BindingSourceType::MouseWheelX:
            case BindingSourceType::MouseWheelY:
                return true;
        }
        return false;
    }
};

constexpr BindingSource::BindingSource(): key(Key::UNKNOWN) {}

struct AxisComposite2D
{
    BindingSource up;
    BindingSource down;
    BindingSource left;
    BindingSource right;
};

struct AnalogStick2D
{
    BindingSource x;
    BindingSource y;
};

enum class BindingShape : uint8_t { Discrete, Axis2DComposite, AnalogStick2D };

struct ActionBinding
{
    ActionHandle action;
    InputContext context{InputContext::Gameplay};
    BindingShape shape{BindingShape::Discrete};
    union
    {
        BindingSource source;
        AxisComposite2D composite;
        AnalogStick2D stick;
    };

    constexpr ActionBinding() : source() {}

    static constexpr ActionBinding Discrete(ActionHandle action, InputContext context, BindingSource source)
    {
        ActionBinding b;
        b.action = action;
        b.context = context;
        b.shape = BindingShape::Discrete;
        b.source = source;
        return b;
    }

    static constexpr ActionBinding Composite(ActionHandle action, InputContext context, AxisComposite2D composite)
    {
        ActionBinding b;
        b.action = action;
        b.context = context;
        b.shape = BindingShape::Axis2DComposite;
        b.composite = composite;
        return b;
    }

    static constexpr ActionBinding Stick(ActionHandle action, InputContext context, AnalogStick2D stick)
    {
        ActionBinding b;
        b.action = action;
        b.context = context;
        b.shape = BindingShape::AnalogStick2D;
        b.stick = stick;
        return b;
    }
};

struct TextInputState
{
    Core::InlineString<32> chars{};
    bool submit{false};
    bool backspace{false};
    bool backspaceDown{false};
    bool deleteForward{false};
    bool deleteForwardDown{false};
    bool left{false};
    bool leftDown{false};
    bool right{false};
    bool rightDown{false};
    bool home{false};
    bool end{false};
    bool up{false};
    bool down{false};
};

struct InputState
{
    InputState() = default;
    explicit InputState(Core::TlsfAllocator* allocator);
    ~InputState() = default;

    Core::Vector<ActionBinding> bindings{};
    Core::Vector<ActionBinding> defaultBindings{};
    Core::Map<ActionHandle, size_t> actionIndex{};
    Core::Vector<Core::ActionState> actionStates{};

    bool bCaptureActive{false};
    size_t captureTargetBindingRow{~size_t{0}};
    bool bBindingsDirty{false};

    Vec2 mousePositionAbsolute{};
    TextInputState textInput{};

    // Just something to get clay to work with my decoupled game/render.
    Vec2 uiScrollAccum{};

    [[nodiscard]] const Core::ActionState& GetActionState(ActionHandle action) const
    {
        static constexpr Core::ActionState ACTION_STATE_EMPTY{};
        const size_t* idx = actionIndex.Find(action);
        return idx ? actionStates[*idx] : ACTION_STATE_EMPTY;
    }
};
} // Engine

#endif //WILL_ENGINE_INPUT_BINDING_H
