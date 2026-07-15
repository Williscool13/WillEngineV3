//
// Created by William on 2026-07-14.
//

#ifndef WILL_ENGINE_GAME_UI_H
#define WILL_ENGINE_GAME_UI_H

#include <cstdint>

#include "clay/clay.h"
#include "core/types/math.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game::UI
{
// ---- State ----

/**
 * Retained UI state.
 */
struct Context
{
    uint32_t activeId{0};
    int32_t caret{0};
};

bool IsFocused(const Context& ui, Clay_ElementId id);
void ClearFocus(Context& ui, Clay_ElementId id);

// ---- Behaviors: headless interaction primitives, no rendering of their own ----

/** Persisted per draggable element: press position/value and whether the drag threshold has been crossed. */
struct DragState
{
    bool active{false};
    bool dragging{false};
    Vec2 pressMouse{};
    Vec2 pressValue{};
};

struct DragResult
{
    bool clicked{false};
    bool dragging{false};
};

/**
 * Press-hold-move-release primitive. Accumulates mouse delta into value once past dragThresholdPx, else reports a plain click on release.
 * Used for: window move, resize, thumb-drag, and drag-sliders.
 */
DragResult DragBehavior(Engine::EngineState* state, Clay_ElementId id, DragState& drag, Vec2& value, float dragThresholdPx = 4.0f);

// ---- Widgets ----

struct TextFieldStyle
{
    float fontSize{16.0f};
    float height{28.0f};
    Clay_Color background{35, 35, 40, 255};
    Clay_Color backgroundFocused{50, 50, 60, 255};
    Clay_Color textColor{230, 230, 230, 255};
    Clay_Color caretColor{255, 255, 255, 255};
};

struct TextFieldResult
{
    bool submitted{false};
    bool changed{false};
};

/**
 * Single-line text field. Declares its own Clay element (background + text + caret) and edits buf in place from state->input.textInput. Guarded by IsFocused.
 */
TextFieldResult TextField(Engine::EngineContext* ctx, Engine::EngineState* state, Context& ui, Clay_ElementId id, char* buf, size_t cap, const TextFieldStyle& style = {});

struct ButtonStyle
{
    float height{24.0f};
    float fontSize{14.0f};
    Clay_Color background{50, 50, 58, 255};
    Clay_Color backgroundHovered{68, 68, 80, 255};
    Clay_Color textColor{220, 220, 220, 255};
};

bool Button(Engine::EngineState* state, Clay_ElementId id, const char* label, const ButtonStyle& style = {});

enum class ToggleAction { None, Toggled, Soloed };

struct ToggleButtonStyle
{
    float height{22.0f};
    float fontSize{13.0f};
    Clay_Color activeColor{80, 140, 220, 255};
    Clay_Color inactiveColor{45, 45, 52, 255};
    Clay_Color hoveredColor{60, 60, 70, 255};
    Clay_Color textColor{230, 230, 230, 255};
};

/**
 * Checkbox styled as a button: persistent on/off. Reports intent only, caller applies it since only it knows the sibling set.
 * Click -> Toggled. Shift+click -> Soloed. Caller always treating Toggled as solo too turns the same group into a radio group.
 */
ToggleAction ToggleButton(Engine::EngineState* state, Clay_ElementId id, const char* label, bool active, const ToggleButtonStyle& style = {});

struct TitleBarStyle
{
    float height{28.0f};
    float fontSize{14.0f};
    Clay_Color background{40, 40, 48, 255};
    Clay_Color textColor{230, 230, 230, 255};
};

struct TitleBarResult
{
    bool clicked{false};
};

/**
 * A window's drag handle. Renders a labeled strip and drives DragBehavior against the caller-owned window position.
 */
TitleBarResult TitleBar(Engine::EngineState* state, Clay_ElementId id, const char* title, DragState& drag, Vec2& windowPosition, const TitleBarStyle& style = {});

// ---- Containers ----

/**
 * RAII container.
 * Example usage: `UI::Panel panel(id, cfg);`.
 */
class [[nodiscard]] Panel
{
public:
    Panel(Clay_ElementId id, const Clay_ElementDeclaration& config);
    ~Panel();
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;
};
} // Game::UI

#endif //WILL_ENGINE_GAME_UI_H
