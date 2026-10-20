//
// Created by William on 2026-10-19.
//

#include "game_build.h"

#include <cstring>
#include <string_view>

#include "core/containers/inline_string.h"
#include "engine/engine_api.h"
#include "engine/console/console.h"

namespace Engine
{
#if defined(WILL_GAME_BUILD_DIR) && defined(WILL_GAME_BUILD_CMAKE) && defined(WILL_GAME_BUILD_VCVARS)
static constexpr bool GAME_BUILD_AVAILABLE = true;
static constexpr const char* BUILD_DIR = WILL_GAME_BUILD_DIR;
static constexpr const char* CMAKE = WILL_GAME_BUILD_CMAKE;
static constexpr const char* VCVARS = WILL_GAME_BUILD_VCVARS;
#else
static constexpr bool GAME_BUILD_AVAILABLE = false;
static constexpr const char* BUILD_DIR = "";
static constexpr const char* CMAKE = "";
static constexpr const char* VCVARS = "";
#endif

static void PrintLines(EngineState* state, GameBuild& build, bool bFlush)
{
    build.process.ReadOutput(build.pending);
    size_t start = 0;
    const char* data = build.pending.Data();
    for (size_t i = 0; i < build.pending.Size(); ++i) {
        if (data[i] != '\n') { continue; }
        size_t end = i;
        if (end > start && data[end - 1] == '\r') { --end; }
        const std::string_view line(data + start, end - start);
        if (line.find("LNK1104") != std::string_view::npos && line.find("engine-runtime.dll") != std::string_view::npos) { build.bEngineLocked = true; }
        Console::Print(state, Core::InlineString<256>(line).c_str());
        start = i + 1;
    }
    if (bFlush && start < build.pending.Size()) {
        Console::Print(state, Core::InlineString<256>(std::string_view(data + start, build.pending.Size() - start)).c_str());
        start = build.pending.Size();
    }
    if (start > 0) {
        const size_t rest = build.pending.Size() - start;
        memmove(build.pending.Data(), build.pending.Data() + start, rest);
        build.pending.Resize(rest);
    }
}

static bool StartBuild(EngineState* state, GameBuild& build)
{
    const auto command = Core::InlineString<Platform::ChildProcess::MAX_COMMAND_LINE>::Format("\"%s\" --build \"%s\" --target game", CMAKE, BUILD_DIR);
    build.pending.Clear();
    if (!build.process.Start(command.c_str(), build.environment.Data())) {
        Console::Print(state, "  w.build_game: could not start the build, see the log");
        build.bBuilding = false;
        return false;
    }
    return true;
}

/** `set` output, one KEY=VALUE per line, into a CreateProcess environment block. */
static void BuildEnvironmentBlock(GameBuild& build)
{
    build.environment.Clear();
    const char* data = build.pending.Data();
    size_t start = 0;
    for (size_t i = 0; i <= build.pending.Size(); ++i) {
        if (i < build.pending.Size() && data[i] != '\n') { continue; }
        size_t end = i;
        if (end > start && data[end - 1] == '\r') { --end; }
        const std::string_view line(data + start, end - start);
        if (!line.empty() && line.front() != '=' && line.find('=') != std::string_view::npos) {
            const size_t offset = build.environment.Size();
            build.environment.Resize(offset + line.size() + 1);
            memcpy(build.environment.Data() + offset, line.data(), line.size());
            build.environment[offset + line.size()] = '\0';
        }
        start = i + 1;
    }
    build.environment.PushBack('\0');
}

void RegisterGameBuildCommands(EngineState* state, GameBuild* build)
{
    build->pending = Core::Vector<char>(state->allocator);
    build->environment = Core::Vector<char>(state->allocator);
    Console::Register(state, Origin::Engine, "w.build_game",
                      "`w.build_game` rebuilds game.dll in this editor's build tree and hot-reloads it; engine changes fail to link while the editor holds engine-runtime.dll",
                      [build](EngineContext*, EngineState* state, Core::Span<const char*>) {
                          if (!GAME_BUILD_AVAILABLE) {
                              Console::Print(state, "  w.build_game: this build has no build tree to rebuild from");
                              return;
                          }
                          if (build->bBuilding) {
                              Console::Print(state, "  w.build_game: a build is already running");
                              return;
                          }
                          build->startTime = std::chrono::steady_clock::now();
                          build->bBuilding = true;
                          build->bEngineLocked = false;
                          if (!build->environment.IsEmpty()) {
                              if (StartBuild(state, *build)) { Console::Print(state, "  w.build_game: building..."); }
                              return;
                          }
                          // cmd /s strips exactly the outer quotes, so every path keeps its own
                          const auto capture = Core::InlineString<Platform::ChildProcess::MAX_COMMAND_LINE>::Format("cmd.exe /s /c \" call \"%s\" >nul 2>&1 && set \"", VCVARS);
                          build->pending.Clear();
                          if (!build->process.Start(capture.c_str())) {
                              Console::Print(state, "  w.build_game: could not run vcvars64.bat, see the log");
                              build->bBuilding = false;
                              return;
                          }
                          build->bCapturingEnvironment = true;
                          Console::Print(state, "  w.build_game: capturing the MSVC environment (first build only), then building...");
                      });
}

bool HoldGameReload(const GameBuild& build)
{
    return build.bBuilding;
}

bool PollGameBuild(EngineState* state, GameBuild& build)
{
    if (!build.bBuilding) { return false; }

    if (build.bCapturingEnvironment) {
        const bool bCapturing = build.process.IsRunning();
        build.process.ReadOutput(build.pending);
        if (bCapturing) { return false; }
        const uint32_t captureExit = build.process.ExitCode();
        build.process.ReadOutput(build.pending);
        build.process.Close();
        build.bCapturingEnvironment = false;
        if (captureExit != 0) {
            Console::Print(state, Core::InlineString<256>::Format("  w.build_game: vcvars64.bat failed (exit %u)", captureExit).c_str());
            build.bBuilding = false;
            return false;
        }
        BuildEnvironmentBlock(build);
        StartBuild(state, build);
        return false;
    }

    const bool bRunning = build.process.IsRunning();
    PrintLines(state, build, !bRunning);
    if (bRunning) { return false; }

    const uint32_t exitCode = build.process.ExitCode();
    const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - build.startTime).count();
    build.process.Close();
    build.bBuilding = false;

    if (exitCode != 0) {
        Console::Print(state, Core::InlineString<256>::Format("  w.build_game: build failed (exit %u) after %.1f s", exitCode, seconds).c_str());
        if (build.bEngineLocked) { Console::Print(state, "  w.build_game: engine code changed, and this editor holds engine-runtime.dll; close the editor and rebuild"); }
        return false;
    }

    Console::Print(state, Core::InlineString<256>::Format("  w.build_game: built in %.1f s; game.dll reloads if it changed", seconds).c_str());
    return true;
}
}
