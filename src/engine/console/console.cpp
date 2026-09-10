//
// Created by William on 2026-07-15.
//

#include "console.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/logging/engine_log.h"
#include "engine/logging/engine_logger.h"
#include "engine/components/camera_components.h"
#include "engine/components/core_components.h"
#include "engine/input/engine_actions.h"
#include "engine/systems/system_graph.h"
#include "engine/ui/ui_zindex.h"

namespace Engine::Console
{
constexpr size_t HELP_LIST_LIMIT = 40;

static StringID CommandId(const char* name)
{
    return StringID(Hash(name, strlen(name)));
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

bool ExecuteCommand(Engine::EngineContext* ctx, Engine::EngineState* state, const char* line)
{
    Core::InlineString<256> echo("] ");
    echo.Append(line);
    Print(state, echo.c_str());

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
        return false;
    }

    ConsoleState& c = state->console;
    if (const size_t* index = c.commandMapping.Find(CommandId(args[0]))) {
        c.commands[*index].callback(ctx, state, Core::Span<const char*>(args.Data(), args.Size()));
        return true;
    }

    Core::InlineString<256> err("Unknown command: ");
    err.Append(args[0]);
    Print(state, err.c_str());
    return false;
}

void Print(Engine::EngineState* state, const char* text)
{
    ConsoleState& c = state->console;
    if (c.lines.IsFull()) {
        c.lines.RemoveAt(0);
    }
    c.lines.PushBack(Core::InlineString<256>(text));
    c.totalPrinted++;
    c.bScrollToBottom = true;
}

void Register(Engine::EngineState* state, Origin origin, const char* name, const char* help, CommandCallback callback)
{
    ConsoleState& c = state->console;
    const StringID id = CommandId(name);
    if (const size_t* existing = c.commandMapping.Find(id)) {
        Command& cmd = c.commands[*existing];
        assert(cmd.name == name && "console command StringID collision between two differently named commands");
        cmd.help = Core::InlineString<128>(help);
        cmd.callback = std::move(callback);
        cmd.origin = origin;
        return;
    }
    if (c.commands.IsFull()) {
        LOG_ERROR(Engine, "Console command '{}' dropped, MAX_COMMANDS ({}) reached", name, MAX_COMMANDS);
        assert(false && "console command overflow");
        return;
    }
    Command cmd;
    cmd.name = Core::ShortString(name);
    cmd.help = Core::InlineString<128>(help);
    cmd.callback = std::move(callback);
    cmd.origin = origin;
    c.commandMapping.Insert(id, c.commands.Size());
    c.commands.PushBack(std::move(cmd));
}

size_t GetCommandCount(const Engine::EngineState* state)
{
    return state->console.commands.Size();
}

CommandInfo GetCommandInfo(const Engine::EngineState* state, const size_t index)
{
    const Command& cmd = state->console.commands[index];
    return {cmd.name.c_str(), cmd.help.c_str()};
}

void ClearGameCommands(Engine::EngineState* state)
{
    ConsoleState& c = state->console;
    for (size_t i = c.commands.Size(); i-- > 0;) {
        if (c.commands[i].origin == Origin::Game) {
            c.commands.RemoveAt(i);
        }
    }
    c.commandMapping.Clear();
    for (size_t i = 0; i < c.commands.Size(); ++i) {
        c.commandMapping.Insert(CommandId(c.commands[i].name.c_str()), i);
    }
}

void RegisterBuiltinCommands(Engine::EngineState* state)
{
    Register(state, Origin::Engine, "help", "List commands; `help <prefix>` narrows the list", [](Engine::EngineContext*, Engine::EngineState* state, Core::Span<const char*> args) {
        const ConsoleState& c = state->console;
        const char* prefix = args.Size() > 1 ? args[1] : "";
        const size_t prefixLen = strlen(prefix);
        if (prefixLen == 0 && c.commands.Size() > HELP_LIST_LIMIT) {
            Print(state, Core::InlineString<256>::Format("  %zu commands registered; use `help <prefix>` to list a subset", c.commands.Size()).c_str());
            return;
        }
        for (auto& cmd : c.commands) {
            if (strncmp(cmd.name.c_str(), prefix, prefixLen) != 0) { continue; }
            Core::InlineString<256> l("  ");
            l.Append(cmd.name);
            l.Append(" - ");
            l.Append(cmd.help);
            Print(state, l.c_str());
        }
    });

    Register(state, Origin::Engine, "clear", "Clear the console output", [](Engine::EngineContext*, Engine::EngineState* state, Core::Span<const char*>) {
        state->console.lines.Clear();
    });

    Register(state, Origin::Engine, "echo", "Print the arguments back", [](Engine::EngineContext*, Engine::EngineState* state, Core::Span<const char*> args) {
        Core::InlineString<256> l;
        for (size_t i = 1; i < args.Size(); ++i) {
            if (i > 1) {
                l.Append(" ");
            }
            l.Append(args[i]);
        }
        Print(state, l.c_str());
    });

    Register(state, Origin::Engine, "render_reset", "Full renderer cache clear", [](Engine::EngineContext*, Engine::EngineState* state, Core::Span<const char*>) {
        state->requests.pendingCacheReset = Core::RenderCacheReset::All;
    });

    Register(state, Origin::Engine, "gtao", "`gtao 0|1` toggles ambient occlusion", [](Engine::EngineContext*, Engine::EngineState* state, Core::Span<const char*> args) {
        if (args.Size() > 1) {
            state->lighting.gtaoConfig.bEnabled = args[1][0] != '0';
        }
        Print(state, state->lighting.gtaoConfig.bEnabled ? "  gtao on" : "  gtao off");
    });

    Register(state, Origin::Engine, "system_graph_dump", "Print the next frame's SystemGraph waves and access sets", [](Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const char*>) {
        ctx->systemGraph->RequestDump();
        Print(state, "  dumping next frame");
    });

    Register(state, Origin::Engine, "log_rdg", "Log the next frame's render graph", [](Engine::EngineContext*, Engine::EngineState* state, Core::Span<const char*>) {
        state->requests.bLogRDG = true;
        Print(state, "  logging next render frame");
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

static void UpdateLogFilters(Engine::EngineContext* ctx, Engine::EngineState* state, ConsoleState& c)
{
    if (!c.bLogFiltersInit) {
        for (bool& f : c.logLevelFilter) { f = true; }
        c.logLevelFilter[0] = false;
        for (bool& f : c.logCategoryFilter) { f = true; }
        for (bool& f : c.gameSubCategoryFilter) { f = true; }
        c.bLogFiltersInit = true;
    }

    for (int i = 0; i < ConsoleState::LOG_LEVEL_COUNT; ++i) {
        const UI::ToggleAction action = UI::ToggleButton(state, CLAY_IDI("Console_LogLevel", i));
        if (action == UI::ToggleAction::Toggled) {
            c.logLevelFilter[i] = !c.logLevelFilter[i];
        }
        else if (action == UI::ToggleAction::Soloed) {
            for (bool& f : c.logLevelFilter) { f = false; }
            c.logLevelFilter[i] = true;
        }
    }

    for (int i = 0; i < static_cast<int>(Engine::LogCategory::Count); ++i) {
        const UI::ToggleAction action = UI::ToggleButton(state, CLAY_IDI("Console_LogCategory", i));
        if (action == UI::ToggleAction::Toggled) {
            c.logCategoryFilter[i] = !c.logCategoryFilter[i];
        }
        else if (action == UI::ToggleAction::Soloed) {
            for (bool& f : c.logCategoryFilter) { f = false; }
            c.logCategoryFilter[i] = true;
        }
    }

    if (ctx->engineLogger) {
        const auto subCategoryNames = ctx->engineLogger->GetImGuiSink()->GetGameSubCategoryNames();
        for (int i = 0; i < static_cast<int>(subCategoryNames.Size()) && i < ConsoleState::MAX_GAME_SUBCATEGORIES; ++i) {
            const UI::ToggleAction action = UI::ToggleButton(state, CLAY_IDI("Console_LogGameSubCategory", i));
            if (action == UI::ToggleAction::Toggled) {
                c.gameSubCategoryFilter[i] = !c.gameSubCategoryFilter[i];
            }
            else if (action == UI::ToggleAction::Soloed) {
                for (bool& f : c.gameSubCategoryFilter) { f = false; }
                c.gameSubCategoryFilter[i] = true;
            }
        }
    }
}

static void UpdateWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ConsoleState& c = state->console;

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
    if (UI::ToggleButton(state, CLAY_ID("Console_Wrap")) != UI::ToggleAction::None) {
        c.bWrapOutput = !c.bWrapOutput;
    }

    const Clay_ElementId activeContentId = c.activeTab == 0 ? CLAY_ID("Console_Output") : CLAY_ID("Console_Log");
    if (state->input.GetActionState(Engine::Actions::ACTION_UI_PAGE_UP).pressed) {
        PageScroll(activeContentId, 1.0f);
    }
    if (state->input.GetActionState(Engine::Actions::ACTION_UI_PAGE_DOWN).pressed) {
        PageScroll(activeContentId, -1.0f);
    }

    if (c.activeTab == 0) {
        UpdateConsoleTabScroll(c);
    }
    else {
        UpdateLogFilters(ctx, state, c);
        UpdateLogTabScroll(c);
    }

    const UI::TextFieldResult res = UI::TextField(ctx, state, c.uiFocus, inputId, c.input, sizeof(c.input));
    if (res.submitted) {
        if (c.input[0] != '\0') {
            PushHistory(c, c.input);
            ExecuteCommand(ctx, state, c.input);
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

static void SnapEditorCameraToGameCamera(Engine::EngineState* state)
{
    auto gameCamView = state->registry.view<Component::GameCameraTag, Component::TransformComponent>();
    auto editorCamView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
    if (gameCamView.front() == entt::null || editorCamView.front() == entt::null) {
        return;
    }
    const auto& gameT = gameCamView.get<Component::TransformComponent>(gameCamView.front());
    auto& editorT = editorCamView.get<Component::TransformComponent>(editorCamView.front());
    editorT.translation = gameT.translation;
    editorT.rotation = gameT.rotation;
}

void Update(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ConsoleState& c = state->console;

    if (c.bOwnsContext && state->inputContext != Engine::InputContext::Console) {
        c.bOpen = false;
        c.bOwnsContext = false;
        ctx->setTextInputActiveFn(false);
    }

    if (state->input.GetActionState(Engine::Actions::ACTION_TOGGLE_CONSOLE).pressed) {
        c.bOpen = !c.bOpen;
        if (c.bOpen) {
            c.bReclaimFocus = true;
            c.bLogScrollToBottom = true;
            state->input.textInput.chars.Clear();
        }
    }
    if (c.bOpen && state->input.GetActionState(Engine::Actions::ACTION_ESCAPE).pressed) {
        c.bOpen = false;
    }

    if (c.bOpen) {
        UpdateWindow(ctx, state);
    }

    if (c.bOpen && !c.bOwnsContext) {
        c.prevContext = state->inputContext;
        if (c.prevContext == Engine::InputContext::Gameplay) {
            SnapEditorCameraToGameCamera(state);
        }
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

static void DrawConsoleTab(Engine::EngineContext* ctx, const ConsoleState& c)
{
    const Clay_TextElementConfigWrapMode wrapMode = c.bWrapOutput ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;

    CLAY(CLAY_ID("Console_Output"), {
         .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(4), .childGap = 2, .layoutDirection = CLAY_TOP_TO_BOTTOM },
         .backgroundColor = {20, 20, 24, 255},
         .clip = { .horizontal = !c.bWrapOutput, .vertical = true, .childOffset = Clay_GetScrollOffset() },
         }) {
        for (auto& line : c.lines) {
            const Clay_String text{.isStaticallyAllocated = false, .length = static_cast<int32_t>(line.Size()), .chars = line.c_str()};
            CLAY_TEXT(text, { .textColor = {220, 220, 220, 255}, .fontSize = 14, .wrapMode = wrapMode });
        }
    }
}

constexpr Clay_Color LOG_LAYER_ACTIVE_COLOR = {70, 165, 150, 255};

static void DrawLogFilters(Engine::EngineContext* ctx, Engine::EngineState* state, const ConsoleState& c)
{
    static constexpr const char* LEVEL_NAMES[ConsoleState::LOG_LEVEL_COUNT] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical"};

    CLAY(CLAY_ID("Console_LogLevelRow"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 4 } }) {
        for (int i = 0; i < ConsoleState::LOG_LEVEL_COUNT; ++i) {
            const auto level = static_cast<spdlog::level::level_enum>(i);
            UI::ToggleButtonDraw(state, CLAY_IDI("Console_LogLevel", i), LEVEL_NAMES[i], c.logLevelFilter[i], { .activeColor = LevelColor(level) });
        }
    }

    CLAY(CLAY_ID("Console_LogCategoryRow"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 4 } }) {
        for (int i = 0; i < static_cast<int>(Engine::LogCategory::Count); ++i) {
            UI::ToggleButtonDraw(state, CLAY_IDI("Console_LogCategory", i), Engine::kCategoryNames[i], c.logCategoryFilter[i], { .activeColor = LOG_LAYER_ACTIVE_COLOR });
        }
    }

    if (!ctx->engineLogger) { return; }
    const auto subCategoryNames = ctx->engineLogger->GetImGuiSink()->GetGameSubCategoryNames();
    if (subCategoryNames.Size() == 0) { return; }

    CLAY(CLAY_ID("Console_LogGameSubCategoryRow"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = {16, 0, 0, 0}, .childGap = 4 } }) {
        for (int i = 0; i < static_cast<int>(subCategoryNames.Size()) && i < ConsoleState::MAX_GAME_SUBCATEGORIES; ++i) {
            UI::ToggleButtonDraw(state, CLAY_IDI("Console_LogGameSubCategory", i), subCategoryNames[i].c_str(), c.gameSubCategoryFilter[i], { .activeColor = LOG_LAYER_ACTIVE_COLOR });
        }
    }
}

static void DrawLogTab(Engine::EngineContext* ctx, Engine::EngineState* state, const ConsoleState& c)
{
    const Clay_TextElementConfigWrapMode wrapMode = c.bWrapOutput ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;

    CLAY(CLAY_ID("Console_LogTab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 4, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        DrawLogFilters(ctx, state, c);

        CLAY(CLAY_ID("Console_Log"), {
             .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(4), .childGap = 2, .layoutDirection = CLAY_TOP_TO_BOTTOM },
             .backgroundColor = {20, 20, 24, 255},
             .clip = { .horizontal = !c.bWrapOutput, .vertical = true, .childOffset = Clay_GetScrollOffset() },
             }) {
            if (ctx->engineLogger) {
                for (const auto& entry : ctx->engineLogger->GetImGuiSink()->GetEntries()) {
                    if (!c.logLevelFilter[static_cast<int>(entry.level)]) { continue; }

                    const bool hasSubCategory = entry.category == Engine::LogCategory::Game && entry.gameSubCategory >= 0
                        && entry.gameSubCategory < ConsoleState::MAX_GAME_SUBCATEGORIES;
                    if (hasSubCategory) {
                        if (!c.gameSubCategoryFilter[entry.gameSubCategory]) { continue; }
                    }
                    else if (!c.logCategoryFilter[static_cast<int>(entry.category)]) {
                        continue;
                    }

                    const Clay_String text{.isStaticallyAllocated = false, .length = static_cast<int32_t>(entry.message.Size()), .chars = entry.message.c_str()};
                    CLAY_TEXT(text, { .textColor = LevelColor(entry.level), .fontSize = 14, .wrapMode = wrapMode });
                }
            }
        }
    }
}

constexpr uint16_t RESIZE_HANDLE_SIZE = 14;
constexpr uint16_t RESIZE_HANDLE_OFFSET = 2;
constexpr uint16_t WRAP_TOGGLE_HEIGHT = 22;
constexpr uint16_t WRAP_TOGGLE_GAP = 6;
constexpr uint16_t BOTTOM_CONTROLS_CLEARANCE = RESIZE_HANDLE_OFFSET + WRAP_TOGGLE_HEIGHT + 4;

static void DrawWindow(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ConsoleState& c = state->console;

    UI::Panel window(CLAY_ID("Console_Window"), {
         .layout = { .sizing = { CLAY_SIZING_FIXED(c.windowSize.x), CLAY_SIZING_FIXED(c.windowSize.y) }, .padding = {0, 0, 0, BOTTOM_CONTROLS_CLEARANCE}, .layoutDirection = CLAY_TOP_TO_BOTTOM },
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
            DrawConsoleTab(ctx, c);
        }
        else {
            DrawLogTab(ctx, state, c);
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

    CLAY(CLAY_ID("Console_WrapFloating"), {
         .floating = {
             .offset = { -static_cast<float>(RESIZE_HANDLE_OFFSET + RESIZE_HANDLE_SIZE + WRAP_TOGGLE_GAP), -static_cast<float>(RESIZE_HANDLE_OFFSET) },
             .zIndex = static_cast<int16_t>(UI::ZIndex::CONSOLE_WINDOW + 10),
             .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM },
             .attachTo = CLAY_ATTACH_TO_PARENT,
             },
         }) {
        UI::ToggleButtonDraw(state, CLAY_ID("Console_Wrap"), "Wrap", c.bWrapOutput);
    }
}

void Draw(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->console.bOpen) {
        DrawWindow(ctx, state);
    }
}
} // Engine::Console
