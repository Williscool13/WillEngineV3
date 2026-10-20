//
// Created by William on 2026-10-19.
//

#ifndef WILL_ENGINE_GAME_BUILD_H
#define WILL_ENGINE_GAME_BUILD_H

#include <chrono>
#include <cstdint>

#include "core/containers/vector.h"
#include "platform/child_process.h"

namespace Engine
{
struct EngineState;

/**
 * Rebuilds game.dll in the build tree this editor was built from, holding the DLL watcher's reload until the build ends.
 * An engine change cannot slip in: the running editor holds engine-runtime.dll, so a build that must relink it fails.
 */
struct GameBuild
{
    bool bBuilding{false};
    /** First build only: runs vcvars64.bat once and keeps its environment, so later builds skip its ~1 s. */
    bool bCapturingEnvironment{false};
    Core::Vector<char> environment;
    bool bEngineLocked{false};

    Platform::ChildProcess process;
    Core::Vector<char> pending;
    std::chrono::steady_clock::time_point startTime{};
};

void RegisterGameBuildCommands(EngineState* state, GameBuild* build);

/** Called by the DLL watcher before it reloads; true means skip this reload. */
bool HoldGameReload(const GameBuild& build);

/**
 * Forwards build output to the console; call once a frame.
 * @return true when the build succeeded and game.dll should reload now
 */
bool PollGameBuild(EngineState* state, GameBuild& build);
}

#endif //WILL_ENGINE_GAME_BUILD_H
