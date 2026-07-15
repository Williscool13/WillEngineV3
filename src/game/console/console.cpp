//
// Created by William on 2026-07-15.
//

#include "console.h"

#include <algorithm>
#include <cstring>

#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/logging/engine_logger.h"
#include "game/game_state.h"
#include "game/input/game_actions.h"
#include "game/ui/game_ui.h"
#include "game/ui/ui_zindex.h"

namespace Game::Console
{
constexpr int MAX_COMMANDS = 128;

struct Command
{
    Core::ShortString name;
    Core::InlineString<128> help;
    CommandCallback callback;
};

static Core::InlineVector<Command, MAX_COMMANDS> gCOMMANDS;

static ConsoleState& GetConsole(Engine::EngineContext* ctx)
{
    return ctx->GetGameState<GameState>()->console;
}

static Clay_Color LevelColor(spdlog::level::level_enum level)
{
    switch (level) {
        case spdlog::level::trace: return {140, 140, 140, 255};
        case spdlog::level::debug: return {160, 170, 210, 255};
        case spdlog::level::warn: return {230, 200, 90, 255};
        case spdlog::level::err: return {235, 95, 95, 255};
        case spdlog::level::critical: return {255, 60, 60, 255};
        default: return {220, 220, 220, 255};
    }
}

static void PushHistory(ConsoleState& c, const char* line)
{
    if (!c.history.IsEmpty() && c.history.Back() == line) {
        return;
    }
    if (c.history.IsFull()) {
        c.history.RemoveAt(0);
    }
    c.history.PushBack(Core::InlineString<256>(line));
}

static void Execute(Engine::EngineContext* ctx, Engine::EngineState* state, const char* line)
{
    Core::InlineString<256> echo("] ");
    echo.Append(line);
    Print(ctx, echo.c_str());

    Core::InlineString<256> work(line);
    Core::InlineVector<const char*, 32> args;
    char* p = work.buf;
    while (*p != '\0' && args.Size() < args.GetCapacity()) {
        while (*p == ' ' || *p == '\t') { ++p; }
        if (*p == '\0') {
            break;
        }
        args.PushBack(p);
        while (*p != '\0' && *p != ' ' && *p != '\t') { ++p; }
        if (*p != '\0') {
            *p = '\0';
            ++p;
        }
    }

    if (args.IsEmpty()) {
        return;
    }

    for (auto& cmd : gCOMMANDS) {
        if (cmd.name == args[0]) {
            cmd.callback(ctx, state, Core::Span<const char*>(args.Data(), args.Size()));
            return;
        }
    }

    Core::InlineString<256> err("Unknown command: ");
    err.Append(args[0]);
    Print(ctx, err.c_str());
}

void Print(Engine::EngineContext* ctx, const char* text)
{
    ConsoleState& c = GetConsole(ctx);
    if (c.lines.IsFull()) {
        c.lines.RemoveAt(0);
    }
    c.lines.PushBack(Core::InlineString<256>(text));
    c.bScrollToBottom = true;
}

void Register(const char* name, const char* help, CommandCallback callback)
{
    for (auto& cmd : gCOMMANDS) {
        if (cmd.name == name) {
            cmd.help = Core::InlineString<128>(help);
            cmd.callback = std::move(callback);
            return;
        }
    }
    if (gCOMMANDS.IsFull()) {
        return;
    }
    Command cmd;
    cmd.name = Core::ShortString(name);
    cmd.help = Core::InlineString<128>(help);
    cmd.callback = std::move(callback);
    gCOMMANDS.PushBack(std::move(cmd));
}

void RegisterBuiltinCommands()
{
    Register("help", "List all commands", [](Engine::EngineContext* ctx, Engine::EngineState*, Core::Span<const char*>) {
        for (auto& cmd : gCOMMANDS) {
            Core::InlineString<256> l("  ");
            l.Append(cmd.name);
            l.Append(" - ");
            l.Append(cmd.help);
            Print(ctx, l.c_str());
        }
    });

    Register("clear", "Clear the console output", [](Engine::EngineContext* ctx, Engine::EngineState*, Core::Span<const char*>) {
        GetConsole(ctx).lines.Clear();
    });

    Register("echo", "Print the arguments back", [](Engine::EngineContext* ctx, Engine::EngineState*, Core::Span<const char*> args) {
        Core::InlineString<256> l;
        for (size_t i = 1; i < args.Size(); ++i) {
            if (i > 1) {
                l.Append(" ");
            }
            l.Append(args[i]);
        }
        Print(ctx, l.c_str());
    });
}

static void SnapToBottom(const Clay_ScrollContainerData& sd)
{
    sd.scrollPosition->y = -std::max(0.0f, sd.contentDimensions.height - sd.scrollContainerDimensions.height);
}

static bool UpdateScrollFollow(Clay_ElementId id, float& lastContentHeight, bool& bAtBottom, bool forceBottom)
{
    const Clay_ScrollContainerData sd = Clay_GetScrollContainerData(id);
    if (!sd.found) {
        return false;
    }

    const float maxScroll = std::max(0.0f, sd.contentDimensions.height - sd.scrollContainerDimensions.height);
    if (forceBottom || sd.contentDimensions.height != lastContentHeight) {
        if (forceBottom || bAtBottom) {
            SnapToBottom(sd);
        }
        lastContentHeight = sd.contentDimensions.height;
    }
    bAtBottom = sd.scrollPosition->y <= -maxScroll + 1.0f;
    return true;
}

static void UpdateConsoleTabScroll(ConsoleState& c)
{
    if (UpdateScrollFollow(CLAY_ID("Console_Output"), c.consoleLastContentHeight, c.bConsoleAtBottom, c.bScrollToBottom)) {
        c.bScrollToBottom = false;
    }
}

static void UpdateLogTabScroll(ConsoleState& c)
{
    if (UpdateScrollFollow(CLAY_ID("Console_Log"), c.logLastContentHeight, c.bLogAtBottom, c.bLogScrollToBottom)) {
        c.bLogScrollToBottom = false;
    }
}

static void PageScroll(Clay_ElementId id, float direction)
{
    const Clay_ScrollContainerData sd = Clay_GetScrollContainerData(id);
    if (!sd.found) {
        return;
    }
    const float maxScroll = std::max(0.0f, sd.contentDimensions.height - sd.scrollContainerDimensions.height);
    const float page = sd.scrollContainerDimensions.height * 0.9f;
    sd.scrollPosition->y = std::clamp(sd.scrollPosition->y + direction * page, -maxScroll, 0.0f);
}

constexpr float MIN_WINDOW_WIDTH = 400.0f;
constexpr float MIN_WINDOW_HEIGHT = 250.0f;

static void UpdateWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ConsoleState& c = GetConsole(ctx);

    const Clay_ElementId inputId = CLAY_ID("Console_Input");
    if (c.bReclaimFocus) {
        c.uiFocus.activeId = inputId.id;
        c.uiFocus.caret = static_cast<int32_t>(strlen(c.input));
        c.bReclaimFocus = false;
    }

    if (UI::IsFocused(c.uiFocus, inputId)) {
        const Engine::TextInputState& ti = state->input.textInput;
        const int32_t prevPos = c.historyPos;
        if (ti.up) {
            if (c.historyPos == -1) {
                c.historyPos = static_cast<int32_t>(c.history.Size()) - 1;
            }
            else if (c.historyPos > 0) {
                --c.historyPos;
            }
        }
        else if (ti.down) {
            if (c.historyPos != -1) {
                ++c.historyPos;
                if (c.historyPos >= static_cast<int32_t>(c.history.Size())) {
                    c.historyPos = -1;
                }
            }
        }
        if (prevPos != c.historyPos) {
            const char* replacement = (c.historyPos >= 0) ? c.history[c.historyPos].c_str() : "";
            const size_t len = std::min(strlen(replacement), sizeof(c.input) - 1);
            memcpy(c.input, replacement, len);
            c.input[len] = '\0';
            c.uiFocus.caret = static_cast<int32_t>(len);
        }
    }

    UI::TitleBar(state, CLAY_ID("Console_TitleBar"), c.windowDrag, c.windowPosition);

    if (UI::ToggleButton(state, CLAY_ID("Console_Tab_Console")) != UI::ToggleAction::None) {
        c.activeTab = 0;
    }
    if (UI::ToggleButton(state, CLAY_ID("Console_Tab_Log")) != UI::ToggleAction::None) {
        if (c.activeTab != 1) {
            c.bLogScrollToBottom = true;
        }
        c.activeTab = 1;
    }

    const Clay_ElementId activeContentId = c.activeTab == 0 ? CLAY_ID("Console_Output") : CLAY_ID("Console_Log");
    if (state->input.GetActionState(Actions::ACTION_UI_PAGE_UP).pressed) {
        PageScroll(activeContentId, 1.0f);
    }
    if (state->input.GetActionState(Actions::ACTION_UI_PAGE_DOWN).pressed) {
        PageScroll(activeContentId, -1.0f);
    }

    if (c.activeTab == 0) {
        UpdateConsoleTabScroll(c);
    }
    else {
        UpdateLogTabScroll(c);
    }

    const UI::TextFieldResult res = UI::TextField(ctx, state, c.uiFocus, inputId, c.input, sizeof(c.input));
    if (res.submitted) {
        if (c.input[0] != '\0') {
            PushHistory(c, c.input);
            Execute(ctx, state, c.input);
        }
        c.input[0] = '\0';
        c.historyPos = -1;
    }

    if (!UI::IsFocused(c.uiFocus, inputId)) {
        c.uiFocus.activeId = inputId.id;
    }

    UI::DragBehavior(state, CLAY_ID("Console_ResizeHandle"), c.resizeDrag, c.windowSize);
    c.windowSize.x = std::max(c.windowSize.x, MIN_WINDOW_WIDTH);
    c.windowSize.y = std::max(c.windowSize.y, MIN_WINDOW_HEIGHT);
}

void Update(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ConsoleState& c = GetConsole(ctx);

    if (state->input.GetActionState(Actions::ACTION_TOGGLE_CONSOLE).pressed) {
        c.bOpen = !c.bOpen;
        if (c.bOpen) {
            c.bReclaimFocus = true;
            c.bLogScrollToBottom = true;
            state->input.textInput.chars.Clear();
        }
    }
    if (c.bOpen && state->input.GetActionState(Actions::ACTION_ESCAPE).pressed) {
        c.bOpen = false;
    }

    if (c.bOpen) {
        UpdateWindow(ctx, state);
    }

    if (c.bOpen && !c.bOwnsContext) {
        c.prevContext = state->inputContext;
        state->inputContext = Engine::InputContext::Console;
        ctx->setCursorHiddenFn(false);
        ctx->setTextInputActiveFn(true); // SDL text input is off by default; without this, no SDL_EVENT_TEXT_INPUT ever fires
        c.bOwnsContext = true;
    }
    else if (!c.bOpen && c.bOwnsContext) {
        state->inputContext = c.prevContext;
        ctx->setCursorHiddenFn(c.prevContext == Engine::InputContext::Gameplay);
        ctx->setTextInputActiveFn(false);
        c.bOwnsContext = false;
    }
}

static void DrawConsoleTab(Engine::EngineContext* ctx)
{
    CLAY(CLAY_ID("Console_Output"), {
         .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(4), .childGap = 2, .layoutDirection = CLAY_TOP_TO_BOTTOM },
         .backgroundColor = {20, 20, 24, 255},
         .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
         }) {
        for (auto& line : GetConsole(ctx).lines) {
            const Clay_String text{.isStaticallyAllocated = false, .length = static_cast<int32_t>(line.Size()), .chars = line.c_str()};
            CLAY_TEXT(text, { .textColor = {220, 220, 220, 255}, .fontSize = 14 });
        }
    }
}

static void DrawLogTab(Engine::EngineContext* ctx)
{
    CLAY(CLAY_ID("Console_Log"), {
         .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(4), .childGap = 2, .layoutDirection = CLAY_TOP_TO_BOTTOM },
         .backgroundColor = {20, 20, 24, 255},
         .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
         }) {
        if (ctx->engineLogger) {
            for (const auto& entry : ctx->engineLogger->GetImGuiSink()->GetEntries()) {
                const Clay_String text{.isStaticallyAllocated = false, .length = static_cast<int32_t>(entry.message.Size()), .chars = entry.message.c_str()};
                CLAY_TEXT(text, { .textColor = LevelColor(entry.level), .fontSize = 14 });
            }
        }
    }
}

constexpr uint16_t RESIZE_HANDLE_SIZE = 14;
constexpr uint16_t RESIZE_HANDLE_OFFSET = 2;
constexpr uint16_t RESIZE_HANDLE_CLEARANCE = RESIZE_HANDLE_SIZE + RESIZE_HANDLE_OFFSET + 4;

static void DrawWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ConsoleState& c = GetConsole(ctx);

    UI::Panel window(CLAY_ID("Console_Window"), {
         .layout = { .sizing = { CLAY_SIZING_FIXED(c.windowSize.x), CLAY_SIZING_FIXED(c.windowSize.y) }, .padding = {0, 0, 0, RESIZE_HANDLE_CLEARANCE}, .layoutDirection = CLAY_TOP_TO_BOTTOM },
         .backgroundColor = {28, 28, 34, 245},
         .cornerRadius = CLAY_CORNER_RADIUS(4),
         .floating = {
             .offset = { c.windowPosition.x, c.windowPosition.y },
             .zIndex = UI::ZIndex::CONSOLE_WINDOW,
             .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
             .attachTo = CLAY_ATTACH_TO_ROOT,
             },
         });

    UI::TitleBarDraw(state, CLAY_ID("Console_TitleBar"), "Console");

    CLAY(CLAY_ID("Console_TabRow"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28) }, .padding = {6, 6, 4, 4}, .childGap = 4 } }) {
        UI::ToggleButtonDraw(state, CLAY_ID("Console_Tab_Console"), "Console", c.activeTab == 0);
        UI::ToggleButtonDraw(state, CLAY_ID("Console_Tab_Log"), "Log", c.activeTab == 1);
    }

    CLAY(CLAY_ID("Console_Content"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = {6, 6, 4, 4} } }) {
        if (c.activeTab == 0) {
            DrawConsoleTab(ctx);
        }
        else {
            DrawLogTab(ctx);
        }
    }

    CLAY(CLAY_ID("Console_InputRow"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(32) }, .padding = {6, 6, 4, 4} } }) {
        UI::TextFieldDraw(ctx, state, c.uiFocus, CLAY_ID("Console_Input"), c.input, { .caretZIndex = static_cast<int16_t>(UI::ZIndex::CONSOLE_WINDOW + 1) });
    }

    CLAY(CLAY_ID("Console_ResizeHandle"), {
         .layout = { .sizing = { CLAY_SIZING_FIXED(RESIZE_HANDLE_SIZE), CLAY_SIZING_FIXED(RESIZE_HANDLE_SIZE) } },
         .backgroundColor = {90, 90, 100, 200},
         .cornerRadius = CLAY_CORNER_RADIUS(2),
         .floating = {
             .offset = { -static_cast<float>(RESIZE_HANDLE_OFFSET), -static_cast<float>(RESIZE_HANDLE_OFFSET) },
             .zIndex = static_cast<int16_t>(UI::ZIndex::CONSOLE_WINDOW + 10),
             .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM },
             .attachTo = CLAY_ATTACH_TO_PARENT,
             },
         }) {}
}

void Draw(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (GetConsole(ctx).bOpen) {
        DrawWindow(ctx, state);
    }
}
}
