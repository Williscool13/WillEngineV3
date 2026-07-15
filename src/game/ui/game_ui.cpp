//
// Created by William on 2026-07-14.
//

#include "game_ui.h"

#include <algorithm>
#include <cstring>

#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/resources/font/font_metrics.h"
#include "game/input/game_actions.h"

namespace Game::UI
{
// ---- State ----

bool IsFocused(const Context& ui, Clay_ElementId id)
{
    return ui.activeId == id.id;
}

void ClearFocus(Context& ui, Clay_ElementId id)
{
    if (ui.activeId == id.id) {
        ui.activeId = 0;
    }
}

// ---- Behaviors ----

DragResult DragBehavior(Engine::EngineState* state, Clay_ElementId id, DragState& drag, Vec2& value, float dragThresholdPx)
{
    const bool hovered = Clay_PointerOver(id);
    const Core::ActionState& pointer = state->input.GetActionState(Actions::ACTION_UI_POINTER_DOWN);

    DragResult result{};

    if (pointer.pressed && hovered) {
        drag.active = true;
        drag.dragging = false;
        drag.pressMouse = state->input.mousePositionAbsolute;
        drag.pressValue = value;
    }

    if (drag.active) {
        const Vec2 delta = state->input.mousePositionAbsolute - drag.pressMouse;
        if (!drag.dragging && (delta.x * delta.x + delta.y * delta.y) >= dragThresholdPx * dragThresholdPx) {
            drag.dragging = true;
        }
        if (drag.dragging) {
            value = drag.pressValue + delta;
            result.dragging = true;
        }
        if (pointer.released) {
            if (!drag.dragging) { result.clicked = true; }
            drag.active = false;
            drag.dragging = false;
        }
    }

    return result;
}

// ---- Widgets ----

static int32_t CaretFromLocalX(Engine::AssetManager* assetManager, Engine::FontHandle font, const char* text, int32_t len, float localX, float fontSize)
{
    float x = 0.0f;
    for (int32_t i = 0; i < len; ++i) {
        const float advance = Engine::GlyphAdvance(assetManager, font, static_cast<unsigned char>(text[i]), fontSize);
        if (localX < x + advance * 0.5f) { return i; }
        x += advance;
    }
    return len;
}

static constexpr float TEXT_FIELD_PAD_X = 6.0f;
static constexpr float TEXT_FIELD_PAD_Y = 4.0f;

static constexpr float KEY_REPEAT_DELAY = 0.4f;
static constexpr float KEY_REPEAT_INTERVAL = 0.035f;

static constexpr float CARET_HEIGHT_SCALE = 1.25f;

static bool KeyRepeat(bool pressed, bool down, float deltaTime, float& timer)
{
    if (pressed) {
        timer = KEY_REPEAT_DELAY;
        return true;
    }
    if (!down) { return false; }
    timer -= deltaTime;
    if (timer <= 0.0f) {
        timer = KEY_REPEAT_INTERVAL;
        return true;
    }
    return false;
}

static int32_t WordStartBefore(const char* buf, int32_t caret)
{
    int32_t i = caret;
    while (i > 0 && buf[i - 1] == ' ') { --i; }
    while (i > 0 && buf[i - 1] != ' ') { --i; }
    return i;
}

TextFieldResult TextField(Engine::EngineContext* ctx, Engine::EngineState* state, Context& ui, Clay_ElementId id, char* buf, size_t cap, const TextFieldStyle& style)
{
    TextFieldResult result{};

    const bool hovered = Clay_PointerOver(id);
    const bool pointerPressed = state->input.GetActionState(Actions::ACTION_UI_POINTER_DOWN).pressed;
    auto len = static_cast<int32_t>(strlen(buf));

    if (pointerPressed) {
        if (hovered) {
            ui.activeId = id.id;
            const Clay_ElementData ed = Clay_GetElementData(id);
            if (ed.found) {
                const float viewportOffsetX = static_cast<float>(ctx->windowContext.viewportOffsetX);
                const float localX = state->input.mousePositionAbsolute.x - viewportOffsetX - ed.boundingBox.x;
                ui.caret = CaretFromLocalX(ctx->assetManager, state->uiFont, buf, len, localX, style.fontSize);
            }
            else {
                ui.caret = len;
            }
        }
        else if (IsFocused(ui, id)) {
            ui.activeId = 0;
        }
    }

    const bool focused = IsFocused(ui, id);

    if (focused) {
        ui.caret = std::clamp(ui.caret, 0, len);
        const Engine::TextInputState& textInput = state->input.textInput;

        if (!textInput.chars.IsEmpty()) {
            const auto insertLen = static_cast<int32_t>(textInput.chars.Size());
            if (static_cast<size_t>(len) + insertLen < cap) {
                memmove(buf + ui.caret + insertLen, buf + ui.caret, static_cast<size_t>(len - ui.caret));
                memcpy(buf + ui.caret, textInput.chars.c_str(), static_cast<size_t>(insertLen));
                len += insertLen;
                buf[len] = '\0';
                ui.caret += insertLen;
                result.changed = true;
            }
        }
        const float dt = state->timeFrame->deltaTime;
        const bool backspaceFired = KeyRepeat(textInput.backspace, textInput.backspaceDown, dt, ui.backspaceRepeatTimer);
        if (backspaceFired && ui.caret > 0) {
            const bool ctrlDown = state->input.GetActionState(Actions::ACTION_MODIFIER_CTRL).down;
            const int32_t deleteFrom = ctrlDown ? WordStartBefore(buf, ui.caret) : ui.caret - 1;
            const int32_t deleteCount = ui.caret - deleteFrom;
            memmove(buf + deleteFrom, buf + ui.caret, static_cast<size_t>(len - ui.caret));
            len -= deleteCount;
            ui.caret = deleteFrom;
            buf[len] = '\0';
            result.changed = true;
        }
        const bool deleteFired = KeyRepeat(textInput.deleteForward, textInput.deleteForwardDown, dt, ui.deleteRepeatTimer);
        if (deleteFired && ui.caret < len) {
            memmove(buf + ui.caret, buf + ui.caret + 1, static_cast<size_t>(len - ui.caret - 1));
            --len;
            buf[len] = '\0';
            result.changed = true;
        }
        const bool leftFired = KeyRepeat(textInput.left, textInput.leftDown, dt, ui.leftRepeatTimer);
        const bool rightFired = KeyRepeat(textInput.right, textInput.rightDown, dt, ui.rightRepeatTimer);
        if (leftFired && ui.caret > 0) { --ui.caret; }
        if (rightFired && ui.caret < len) { ++ui.caret; }
        if (textInput.home) { ui.caret = 0; }
        if (textInput.end) { ui.caret = len; }
        if (textInput.submit) { result.submitted = true; }
    }

    return result;
}

void TextFieldDraw(Engine::EngineContext* ctx, Engine::EngineState* state, const Context& ui, Clay_ElementId id, const char* buf, const TextFieldStyle& style)
{
    const bool focused = IsFocused(ui, id);
    const auto len = static_cast<int32_t>(strlen(buf));

    CLAY(id, {
         .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(style.height) }, .padding = {static_cast<uint16_t>(TEXT_FIELD_PAD_X), static_cast<uint16_t>(TEXT_FIELD_PAD_X), static_cast<uint16_t>(TEXT_FIELD_PAD_Y), static_cast<uint16_t>(TEXT_FIELD_PAD_Y)}, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
         .backgroundColor = focused ? style.backgroundFocused : style.background,
         .cornerRadius = CLAY_CORNER_RADIUS(3),
         }) {
        if (len > 0) {
            const Clay_String text{.isStaticallyAllocated = false, .length = len, .chars = buf};
            CLAY_TEXT(text, { .textColor = style.textColor, .fontSize = static_cast<uint16_t>(style.fontSize) });
        }
        if (focused) {
            const float caretX = TEXT_FIELD_PAD_X + Engine::MeasureText(ctx->assetManager, state->uiFont, buf, ui.caret, style.fontSize);
            CLAY(CLAY_ID_LOCAL("Caret"), {
                 .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(style.fontSize) } },
                 .backgroundColor = style.caretColor,
                 .floating = {
                     .offset = {caretX, 0},
                     .zIndex = style.caretZIndex,
                     .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER },
                     .attachTo = CLAY_ATTACH_TO_PARENT,
                     },
                 }) {}
        }
    }
}

static Clay_String ToClayString(const char* text)
{
    return {.isStaticallyAllocated = false, .length = static_cast<int32_t>(strlen(text)), .chars = text};
}

bool Button(Engine::EngineState* state, Clay_ElementId id)
{
    return Clay_PointerOver(id) && state->input.GetActionState(Actions::ACTION_UI_POINTER_DOWN).pressed;
}

void ButtonDraw(Engine::EngineState* state, Clay_ElementId id, const char* label, const ButtonStyle& style)
{
    const bool hovered = Clay_PointerOver(id);

    CLAY(id, {
         .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(style.height) }, .padding = {10, 10, 4, 4}, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
         .backgroundColor = hovered ? style.backgroundHovered : style.background,
         .cornerRadius = CLAY_CORNER_RADIUS(3),
         }) {
        CLAY_TEXT(ToClayString(label), { .textColor = style.textColor, .fontSize = static_cast<uint16_t>(style.fontSize) });
    }
}

ToggleAction ToggleButton(Engine::EngineState* state, Clay_ElementId id)
{
    if (Clay_PointerOver(id) && state->input.GetActionState(Actions::ACTION_UI_POINTER_DOWN).pressed) {
        const bool shift = state->input.GetActionState(Actions::ACTION_MODIFIER_SHIFT).down;
        return shift ? ToggleAction::Soloed : ToggleAction::Toggled;
    }
    return ToggleAction::None;
}

void ToggleButtonDraw(Engine::EngineState* state, Clay_ElementId id, const char* label, bool active, const ToggleButtonStyle& style)
{
    const bool hovered = Clay_PointerOver(id);
    const Clay_Color background = active ? style.activeColor : (hovered ? style.hoveredColor : style.inactiveColor);

    CLAY(id, {
         .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(style.height) }, .padding = {8, 8, 3, 3}, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
         .backgroundColor = background,
         .cornerRadius = CLAY_CORNER_RADIUS(3),
         }) {
        CLAY_TEXT(ToClayString(label), { .textColor = style.textColor, .fontSize = static_cast<uint16_t>(style.fontSize) });
    }
}

TitleBarResult TitleBar(Engine::EngineState* state, Clay_ElementId id, DragState& drag, Vec2& windowPosition)
{
    const DragResult dragResult = DragBehavior(state, id, drag, windowPosition);
    return {dragResult.clicked};
}

void TitleBarDraw(Engine::EngineState* state, Clay_ElementId id, const char* title, const TitleBarStyle& style)
{
    CLAY(id, {
         .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(style.height) }, .padding = {8, 8, 0, 0}, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
         .backgroundColor = style.background,
         }) {
        CLAY_TEXT(ToClayString(title), { .textColor = style.textColor, .fontSize = static_cast<uint16_t>(style.fontSize) });
    }
}

// ---- Containers ----

Panel::Panel(Clay_ElementId id, const Clay_ElementDeclaration& config)
{
    Clay__OpenElementWithId(id);
    Clay__ConfigureOpenElement(config);
}

Panel::~Panel()
{
    Clay__CloseElement();
}
} // Game::UI
