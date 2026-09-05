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
#include "engine/input/engine_actions.h"
#include "game/console/console.h"
#include "game/ui/ui_zindex.h"
#include "core/memory/memory_manager.h"

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
    const Core::ActionState& pointer = state->input.GetActionState(Engine::Actions::ACTION_UI_POINTER_DOWN);

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
    const bool pointerPressed = state->input.GetActionState(Engine::Actions::ACTION_UI_POINTER_DOWN).pressed;
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
            const bool ctrlDown = state->input.GetActionState(Engine::Actions::ACTION_MODIFIER_CTRL).down;
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
                 .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(style.fontSize * CARET_HEIGHT_SCALE) } },
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
    return Clay_PointerOver(id) && state->input.GetActionState(Engine::Actions::ACTION_UI_POINTER_DOWN).pressed;
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
    if (Clay_PointerOver(id) && state->input.GetActionState(Engine::Actions::ACTION_UI_POINTER_DOWN).pressed) {
        const bool shift = state->input.GetActionState(Engine::Actions::ACTION_MODIFIER_SHIFT).down;
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

namespace Game
{
static const char* DenoiserModeName(Core::ReSTIRParams::DenoiserMode mode)
{
    switch (mode) {
        case Core::ReSTIRParams::DenoiserMode::None: return "None";
        case Core::ReSTIRParams::DenoiserMode::ATrous: return "ATrous";
        case Core::ReSTIRParams::DenoiserMode::ASVGF: return "ASVGF";
        case Core::ReSTIRParams::DenoiserMode::RELAX: return "RELAX";
        case Core::ReSTIRParams::DenoiserMode::ReBLUR: return "ReBLUR";
        case Core::ReSTIRParams::DenoiserMode::NRD: return "NRD";
    }
    return "None";
}

static void AppendDiffuseGIModeText(Core::InlineString<48>& out, const Engine::LightingState& lighting)
{
    if (lighting.groundTruthMode == Core::GroundTruthMode::GI || lighting.groundTruthMode == Core::GroundTruthMode::Full) {
        out.Append("Ground Truth");
        return;
    }
    if (!lighting.ddgi.bEnabled) {
        out.Append("Off");
        return;
    }
    out.Append("DDGI");
    if (lighting.ddgi.bFinalGather) {
        out.Append(" + Final Gather");
    }
}

void GatherUIRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Clay_SetLayoutDimensions({static_cast<float>(ctx->windowContext.viewportWidth), static_cast<float>(ctx->windowContext.viewportHeight)});

    Clay_UpdateScrollContainers(true, Clay_Vector2{state->input.uiScrollAccum.x, state->input.uiScrollAccum.y}, frameBuffer->timeFrame.deltaTime);
    state->input.uiScrollAccum = {};

    Clay_BeginLayout();

#ifdef WDEBUG
    Game::Console::Draw(ctx, state);
#endif

#if WILL_EDITOR
    if (state->debug.bEnableUI) {
#endif
#if 0
        constexpr Clay_Color COLOR_LIGHT = Clay_Color{224, 215, 210, 255};
        constexpr Clay_Color COLOR_RED = Clay_Color{168, 66, 28, 255};
        constexpr Clay_Color COLOR_ORANGE = Clay_Color{225, 138, 50, 255};

        uint32_t smilingFriendImageIndex = SMILING_FRIENDS_BINDLESS_INDEX;

        constexpr Clay_Color COLOR_DARK = Clay_Color{30, 30, 40, 240};
        constexpr Clay_Color COLOR_ITEM = Clay_Color{60, 80, 120, 255};
        constexpr Clay_Color COLOR_SCROLLBAR = Clay_Color{180, 180, 200, 160};
        constexpr Clay_Color COLOR_OVERLAY = Clay_Color{255, 120, 60, 80};

        CLAY(CLAY_ID("OuterContainer"), { .layout = { .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = {250, 250, 255, 64} }) {
            CLAY(CLAY_ID("SideBar"), {
                 .layout = {.sizing = {.width = CLAY_SIZING_FIXED(300), .height = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .layoutDirection = CLAY_TOP_TO_BOTTOM, },
                 .backgroundColor = COLOR_LIGHT
                 }) {
                CLAY(CLAY_ID("ProfilePictureOuter"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = COLOR_RED }) {
                    CLAY(CLAY_ID("ProfilePicture"), { .layout = { .sizing = { .width = CLAY_SIZING_FIXED(60), .height = CLAY_SIZING_FIXED(60) }}, .image = { .imageData = &smilingFriendImageIndex } }) {}
                    CLAY_TEXT(CLAY_STRING("Clay - UI Library"), { .textColor = {255, 255, 255, 255}, .fontSize = 24, });
                }
                CLAY_TEXT(CLAY_STRING("WillEngine"), {.textColor = {255, 255, 255, 255}, .fontSize = 24, });

                CLAY(CLAY_ID("MainContent"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) } }, .backgroundColor = COLOR_LIGHT }) {}
            }

            // Border demo: nested boxes showing per-side widths and corner radius
            CLAY(CLAY_ID("BorderDemo"), {
                 .layout = { .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200) }, .padding = CLAY_PADDING_ALL(16), .childGap = 12, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                 .backgroundColor = {20, 20, 30, 255},
                 .border = { .color = {100, 200, 255, 255}, .width = { .left = 3, .right = 3, .top = 3, .bottom = 3 } },
                 }) {
                CLAY(CLAY_ID("BorderInner1"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = {40, 40, 60, 255},
                     .cornerRadius = CLAY_CORNER_RADIUS(8),
                     .border = { .color = {255, 180, 50, 255}, .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 } },
                     }) {
                    CLAY_TEXT(CLAY_STRING("Rounded"), { .textColor = {220, 220, 255, 255}, .fontSize = 18 });
                }
                CLAY(CLAY_ID("BorderInner2"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = {40, 40, 60, 255},
                     .border = { .color = {80, 255, 120, 255}, .width = { .left = 6, .right = 1, .top = 1, .bottom = 6 } },
                     }) {
                    CLAY_TEXT(CLAY_STRING("Asymmetric"), { .textColor = {220, 220, 255, 255}, .fontSize = 18 });
                }
            }

            // Rounded image demo
            CLAY(CLAY_ID("RoundedImage"), {
                 .layout = { .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(120) } },
                 .cornerRadius = CLAY_CORNER_RADIUS(20),
                 .image = { .imageData = &smilingFriendImageIndex },
                 }) {}

            // Overlay demo: a panel whose overlayColor tints all children
            CLAY(CLAY_ID("OverlayDemo"), {
                 .layout = { .sizing = { CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(200) }, .padding = CLAY_PADDING_ALL(10), .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                 .backgroundColor = {50, 50, 80, 255},
                 .overlayColor = { 80, 160, 255, 100 },
                 .border = { .color = {180, 180, 255, 200}, .width = { .left = 1, .right = 1, .top = 1, .bottom = 1 } },
                 }) {
                CLAY_TEXT(CLAY_STRING("Overlay"), { .textColor = {255, 255, 255, 255}, .fontSize = 20 });
                CLAY(CLAY_ID("OverlayItem1"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 8, 8, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = COLOR_RED,
                     .cornerRadius = CLAY_CORNER_RADIUS(4),
                     }) {
                    CLAY_TEXT(CLAY_STRING("Red"), { .textColor = {255, 255, 255, 255}, .fontSize = 16 });
                }
                CLAY(CLAY_ID("OverlayItem2"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 8, 8, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = COLOR_ORANGE,
                     .cornerRadius = CLAY_CORNER_RADIUS(4),
                     }) {
                    CLAY_TEXT(CLAY_STRING("Orange"), { .textColor = {255, 255, 255, 255}, .fontSize = 16 });
                }
            }

            // Scrollable list with overlay color tint and a floating scrollbar
            CLAY(CLAY_ID("ScrollDemo"), {
                 .layout = { .sizing = { .width = CLAY_SIZING_FIXED(260), .height = CLAY_SIZING_FIXED(300) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                 .backgroundColor = COLOR_DARK,
                 .overlayColor = COLOR_OVERLAY,
                 }) {
                // Clipped scroll area (generates SCISSOR_START/END)
                CLAY(CLAY_ID("ScrollList"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 6, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                     .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
                     }) {
                    for (int32_t i = 0; i < 32; ++i) {
                        CLAY(CLAY_IDI("ScrollItem", i), {
                             .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(36) }, .padding = { 8, 8, 6, 6 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                             .backgroundColor = COLOR_ITEM,
                             .cornerRadius = CLAY_CORNER_RADIUS(4),
                             }) {
                            CLAY_TEXT(CLAY_STRING("Item"), { .textColor = {220, 220, 255, 255}, .fontSize = 18 });
                        }
                    }
                }

                // Floating scrollbar thumb
                Clay_ScrollContainerData scrollData = Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("ScrollList")));
                if (scrollData.found && scrollData.contentDimensions.height > scrollData.scrollContainerDimensions.height) {
                    const float trackH = scrollData.scrollContainerDimensions.height;
                    const float thumbH = (trackH / scrollData.contentDimensions.height) * trackH;
                    const float thumbY = (-scrollData.scrollPosition->y / scrollData.contentDimensions.height) * trackH;
                    CLAY(CLAY_ID("ScrollThumb"), {
                         .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(thumbH) } },
                         .backgroundColor = COLOR_SCROLLBAR,
                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                         .floating = {
                         .offset = { .x = -6, .y = thumbY },
                         .parentId = Clay_GetElementId(CLAY_STRING("ScrollList")).id,
                         .zIndex = UI::ZIndex::LIST_DECORATION,
                         .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                         .attachTo = CLAY_ATTACH_TO_PARENT,
                         },
                         }) {}
                }
            }
        }
#endif

        const Core::TimeFrame& tf = frameBuffer->timeFrame;
        const auto renderFpsText = Core::InlineString<48>::Format("Render: %.0f FPS (%.2f ms)", tf.renderWallMs > 0.0f ? 1000.0f / tf.renderWallMs : 0.0f, tf.renderWallMs);
        const auto gpuText = Core::InlineString<48>::Format("GPU: %.2f ms", tf.gpuFrameMs);
        const auto gameFpsText = Core::InlineString<48>::Format("Game: %.0f FPS (%.2f ms)", tf.gameFps, tf.gameFps > 0.0f ? 1000.0f / tf.gameFps : 0.0f);
        const Clay_String renderFpsString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(renderFpsText.Size()), .chars = renderFpsText.c_str()};
        const Clay_String gpuString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(gpuText.Size()), .chars = gpuText.c_str()};
        const Clay_String gameFpsString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(gameFpsText.Size()), .chars = gameFpsText.c_str()};

        static constexpr const char* AA_MODE_NAMES[] = {"None", "SMAA", "TAA", "SMAA T2X", "Naive TAA", "Donut TAA"};
        const char* aaModeName = AA_MODE_NAMES[static_cast<int32_t>(state->lighting.aaConfig.mode)];
        const char* profileName = state->projectConfig.activeLightingProfile.IsEmpty() ? "(none)" : state->projectConfig.activeLightingProfile.c_str();
        const auto profileText = Core::InlineString<80>::Format("Profile: %s", profileName);
        const auto aaText = Core::InlineString<48>::Format("AA: %s | Denoiser: %s", aaModeName, DenoiserModeName(state->debug.restir.denoiserMode));
        Core::InlineString<48> giText("Diffuse GI: ");
        AppendDiffuseGIModeText(giText, state->lighting);

        const auto renderWidth = static_cast<uint32_t>(static_cast<float>(ctx->windowContext.viewportWidth) * state->projectConfig.resolutionScale);
        const auto renderHeight = static_cast<uint32_t>(static_cast<float>(ctx->windowContext.viewportHeight) * state->projectConfig.resolutionScale);
        const auto resText = Core::InlineString<64>::Format("Res: %ux%u (%.0f%%)", renderWidth, renderHeight, state->projectConfig.resolutionScale * 100.0f);

        const Core::PostProcessConfiguration& pp = state->lighting.postProcess;
        Core::InlineString<160> ppSummary("PP:");
        bool bAnyPPActive = false;
        auto appendPPTag = [&](bool bEnabled, const char* tag) {
            if (!bEnabled) { return; }
            ppSummary.Append(bAnyPPActive ? ", " : " ");
            ppSummary.Append(tag);
            bAnyPPActive = true;
        };
        appendPPTag(state->lighting.gtaoConfig.bEnabled, "GTAO");
        appendPPTag(pp.bExposureEnabled, "Exposure");
        appendPPTag(pp.bBloomEnabled, "Bloom");
        appendPPTag(pp.bMotionBlurEnabled, "MotionBlur");
        appendPPTag(pp.bColorGradingEnabled, "ColorGrade");
        appendPPTag(pp.bVignetteEnabled, "Vignette");
        appendPPTag(pp.bChromaticAberrationEnabled, "ChromAb");
        appendPPTag(pp.bSharpeningEnabled, "Sharpen");
        appendPPTag(pp.bPaniniEnabled, "Panini");
        appendPPTag(pp.bFilmGrainEnabled, "FilmGrain");
        appendPPTag(pp.bDitherEnabled, "Dither");
        if (!bAnyPPActive) { ppSummary.Append(" None"); }

        static Core::InlineString<80> memText("Mem: ...");
        static float memRefreshTimer = 1.0f;
        memRefreshTimer += tf.deltaTime;
        if (memRefreshTimer >= 1.0f) {
            memRefreshTimer = 0.0f;
            const Core::MemoryManager::Stats ms = ctx->memoryManager->GetStats();
            const Core::TlsfAllocator::Stats* pools[] = {&ms.persistent, &ms.general, &ms.assetsScratch, &ms.assets, &ms.physics, &ms.render, &ms.vulkan};
            size_t usedBytes = ms.virtualMemory.committedBytes;
            size_t committedBytes = ms.virtualMemory.committedBytes;
            size_t budgetBytes = ms.virtualMemory.reservedBytes;
            for (const Core::TlsfAllocator::Stats* pool : pools) {
                usedBytes += pool->usedBytes;
                committedBytes += pool->totalBytes;
                budgetBytes += pool->budgetBytes > 0 ? pool->budgetBytes : pool->totalBytes;
            }
            constexpr float kToMB = 1.0f / (1024.0f * 1024.0f);
            memText = Core::InlineString<80>::Format("Mem: %.0f / %.0f cmt / %.0f MB", static_cast<float>(usedBytes) * kToMB, static_cast<float>(committedBytes) * kToMB, static_cast<float>(budgetBytes) * kToMB);
        }
        const Clay_String memString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(memText.Size()), .chars = memText.c_str()};

        const Clay_String profileString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(profileText.Size()), .chars = profileText.c_str()};
        const Clay_String aaString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(aaText.Size()), .chars = aaText.c_str()};
        const Clay_String giString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(giText.Size()), .chars = giText.c_str()};
        const Clay_String resString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(resText.Size()), .chars = resText.c_str()};
        const Clay_String ppString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(ppSummary.Size()), .chars = ppSummary.c_str()};

        CLAY(CLAY_ID("FpsCounter"), {
             .layout = { .sizing = { .width = CLAY_SIZING_FIXED(340) }, .padding = CLAY_PADDING_ALL(8), .childGap = 4, .layoutDirection = CLAY_TOP_TO_BOTTOM },
             .backgroundColor = {0, 0, 0, 140},
             .cornerRadius = CLAY_CORNER_RADIUS(4),
             .floating = {
             .offset = { .x = 16, .y = -16 },
             .zIndex = UI::ZIndex::HUD_OVERLAY,
             .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
             .attachTo = CLAY_ATTACH_TO_ROOT,
             },
             }) {
            CLAY_TEXT(renderFpsString, { .textColor = {255, 255, 255, 255}, .fontSize = 16 });
            CLAY_TEXT(gpuString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(gameFpsString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(profileString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(aaString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(giString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(resString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(ppString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(memString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
        }
#if WILL_EDITOR
    } // state->debug.bEnableUI
#endif

    Clay_RenderCommandArray renderCommands = Clay_EndLayout(frameBuffer->timeFrame.deltaTime);

    const auto vpWidth = static_cast<float>(ctx->windowContext.viewportWidth);
    const auto vpHeight = static_cast<float>(ctx->windowContext.viewportHeight);

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    const Engine::Font* uiFont = ctx->assetManager->GetFont(state->uiFont);

    for (int32_t i = 0; i < renderCommands.length; ++i) {
        const Clay_RenderCommand& cmd = renderCommands.internalArray[i];

        switch (cmd.commandType) {
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                Core::UIDrawCommand dc{.type = Core::UICommandType::ScissorPush};
                dc.scissor = Core::UIScissorCommand{static_cast<int32_t>(bb.x), static_cast<int32_t>(bb.y), static_cast<uint32_t>(bb.width), static_cast<uint32_t>(bb.height)};
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            {
                vf.uiDrawList.PushBack(Core::UIDrawCommand{.type = Core::UICommandType::ScissorPop});
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
            {
                const Clay_Color& c = cmd.renderData.overlayColor.color;
                Core::UIDrawCommand dc{.type = Core::UICommandType::OverlayPush};
                dc.overlay = Core::UIOverlayColorCommand{.color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}};
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
            {
                vf.uiDrawList.PushBack(Core::UIDrawCommand{.type = Core::UICommandType::OverlayPop});
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_Color& c = cmd.renderData.rectangle.backgroundColor;
                const Clay_CornerRadius& cr = cmd.renderData.rectangle.cornerRadius;
                Core::UIDrawCommand dc{.type = Core::UICommandType::Rect};
                dc.rect = Core::UIRectDrawCall{
                    .color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f},
                    .cornerRadius = {cr.topLeft, cr.topRight, cr.bottomLeft, cr.bottomRight},
                    .pxMin = {bb.x, bb.y},
                    .pxMax = {bb.x + bb.width, bb.y + bb.height},
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_ImageRenderData& img = cmd.renderData.image;
                const uint32_t bindlessIndex = *static_cast<const uint32_t*>(img.imageData);
                const Clay_Color& tc = img.backgroundColor;
                const bool bUntinted = tc.r == 0 && tc.g == 0 && tc.b == 0 && tc.a == 0;
                const Vec4 tint = bUntinted
                                      ? Vec4{1.0f, 1.0f, 1.0f, 1.0f}
                                      : Vec4{tc.r / 255.0f, tc.g / 255.0f, tc.b / 255.0f, tc.a / 255.0f};
                const Clay_CornerRadius& cr = img.cornerRadius;
                Core::UIDrawCommand dc{.type = Core::UICommandType::Image};
                dc.image = Core::UIRenderCommandImage{
                    .pxMin = {bb.x, bb.y},
                    .pxMax = {bb.x + bb.width, bb.y + bb.height},
                    .uvMin = {0.0f, 1.0f}, // y flip: viewport Y-flip in SetupUIRender inverts V
                    .uvMax = {1.0f, 0.0f},
                    .tintColor = tint,
                    .cornerRadius = {cr.topLeft, cr.topRight, cr.bottomLeft, cr.bottomRight},
                    .imageBindlessIndex = bindlessIndex,
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_BorderRenderData& bd = cmd.renderData.border;
                const Clay_Color& c = bd.color;
                Core::UIDrawCommand dc{.type = Core::UICommandType::Border};
                dc.border = Core::UIBorderDrawCall{
                    .color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f},
                    .borderWidths = {
                        static_cast<float>(bd.width.left),
                        static_cast<float>(bd.width.right),
                        static_cast<float>(bd.width.top),
                        static_cast<float>(bd.width.bottom),
                    },
                    .cornerRadius = {
                        bd.cornerRadius.topLeft,
                        bd.cornerRadius.topRight,
                        bd.cornerRadius.bottomLeft,
                        bd.cornerRadius.bottomRight,
                    },
                    .pxMin = {bb.x, bb.y},
                    .pxMax = {bb.x + bb.width, bb.y + bb.height},
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT:
            {
                if (!uiFont) { break; }
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_TextRenderData& td = cmd.renderData.text;
                const float fontSize = td.fontSize;
                const float scale = fontSize / uiFont->header.emSize;
                const Vec4 color{td.textColor.r / 255.0f, td.textColor.g / 255.0f, td.textColor.b / 255.0f, td.textColor.a / 255.0f};

                const auto quadStart = static_cast<uint32_t>(vf.uiGlyphQuads.Size());
                uint32_t quadCount = 0;

                float cursorX = bb.x;
                const float baselineY = bb.y + fontSize;

                const float dilatePx = 0.75f;
                const float dilateEm = dilatePx / scale;

                for (int32_t ci = 0; ci < td.stringContents.length; ++ci) {
                    const uint32_t cp = static_cast<unsigned char>(td.stringContents.chars[ci]);
                    const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(state->uiFont, cp);
                    if (!g) {
                        cursorX += fontSize * 0.25f;
                        continue;
                    }

                    if (g->slugTexelCount != 0) {
                        const float xMin = (cursorX + g->planeLeft * scale - dilatePx) / vpWidth * 2.0f - 1.0f;
                        const float xMax = (cursorX + g->planeRight * scale + dilatePx) / vpWidth * 2.0f - 1.0f;
                        const float yMin = (baselineY - g->planeTop * scale - dilatePx) / vpHeight * 2.0f - 1.0f;
                        const float yMax = (baselineY - g->planeBottom * scale + dilatePx) / vpHeight * 2.0f - 1.0f;

                        vf.uiGlyphQuads.PushBack(UIGlyphQuad{
                            .color = {color.x, color.y, color.z, color.w},
                            .posMin = {xMin, yMin},
                            .posMax = {xMax, yMax},
                            .emMin = {g->planeLeft - dilateEm, g->planeTop + dilateEm},
                            .emMax = {g->planeRight + dilateEm, g->planeBottom - dilateEm},
                            .glyphTexelOffset = g->slugTexelOffset,
                        });
                        ++quadCount;
                    }

                    cursorX += g->advance * scale + td.letterSpacing;
                }

                if (quadCount == 0) { break; }

                Core::UIDrawCommand dc{.type = Core::UICommandType::Text};
                dc.text = Core::UITextDrawCall{
                    .quadOffset = quadStart,
                    .quadCount = quadCount,
                    .fontCurveByteOffset = uiFont->curveByteOffset,
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            default: break;
        }
    }
}
} // Game
