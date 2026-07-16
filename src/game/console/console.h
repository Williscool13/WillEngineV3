//
// Created by William on 2026-07-15.
//

#ifndef WILL_ENGINE_CONSOLE_H
#define WILL_ENGINE_CONSOLE_H

#include "core/containers/inline_function.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/containers/span.h"
#include "core/types/math.h"
#include "engine/engine_api.h"
#include "engine/logging/log_category.h"
#include "game/ui/game_ui.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game::Console
{
/** A command handler. args[0] is the command name, args[1..] are the whitespace-split arguments. */
using CommandCallback = Core::InlineFunction<void(Engine::EngineContext*, Engine::EngineState*, Core::Span<const char*>), 64>;

struct ConsoleState
{
    static constexpr size_t MAX_LINES = 512;
    static constexpr size_t MAX_HISTORY = 64;
    static constexpr int LOG_LEVEL_COUNT = 6;
    static constexpr int MAX_GAME_SUBCATEGORIES = 32; // matches ImGuiSink::gameSubCategoryNames capacity

    bool bOpen{false};
    bool bOwnsContext{false};
    Engine::InputContext prevContext{Engine::InputContext::Editor};
    bool bReclaimFocus{false};

    bool bScrollToBottom{false};
    bool bConsoleAtBottom{true};
    float consoleLastContentHeight{0.0f};
    bool bLogScrollToBottom{false};
    bool bLogAtBottom{true};
    float logLastContentHeight{0.0f};

    bool bWrapOutput{true};

    bool bLogFiltersInit{false};
    bool logLevelFilter[LOG_LEVEL_COUNT]{};
    bool logCategoryFilter[static_cast<int>(Engine::LogCategory::Count)]{};
    bool gameSubCategoryFilter[MAX_GAME_SUBCATEGORIES]{};

    int32_t historyPos{-1};
    int32_t activeTab{0};
    char input[256]{};

    Vec2 windowPosition{80.0f, 80.0f};
    UI::DragState windowDrag{};

    Vec2 windowSize{760.0f, 440.0f};
    UI::DragState resizeDrag{};

    UI::Context uiFocus{};

    Core::InlineVector<Core::InlineString<256>, MAX_LINES> lines{};
    Core::InlineVector<Core::InlineString<256>, MAX_HISTORY> history{};
};

void Register(const char* name, const char* help, CommandCallback callback);

void Print(Engine::EngineContext* ctx, const char* text);


/**
 * Help, Clear, and Echo.
 */
void RegisterBuiltinCommands();

void Update(Engine::EngineContext* ctx, Engine::EngineState* state);

void Draw(Engine::EngineContext* ctx, Engine::EngineState* state);
}

#endif //WILL_ENGINE_CONSOLE_H
