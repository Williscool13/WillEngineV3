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
 * Rebuilds game.dll in the build tree this editor was built from. The DLL watcher's reload is held while it builds; afterwards it is
 * released only when no engine library the game links is newer than the running exe, since that DLL would carry a different engine.
 * (A ninja dry run cannot answer this: CONFIGURE_DEPENDS globs make every dry run stop at a pretend CMake re-run.)
 */
struct GameBuild
{
    bool bBuilding{false};
    /** First build only: runs vcvars64.bat once and keeps its environment, so later builds skip its ~1 s. */
    bool bCapturingEnvironment{false};
    Core::Vector<char> environment;
    bool bForce{false};
    /** The watcher saw the new DLL mid-build. */
    bool bReloadHeld{false};
    /** Engine libraries changed under this exe; reloads stay off until a clean w.build_game or a restart. */
    bool bReloadBlocked{false};

    Platform::ChildProcess process;
    Core::Vector<char> pending;
    std::chrono::steady_clock::time_point startTime{};
};

void RegisterGameBuildCommands(EngineState* state, GameBuild* build);

/** Called by the DLL watcher before it reloads; true means skip this reload. */
bool HoldGameReload(EngineState* state, GameBuild& build);

/**
 * Forwards build output to the console; call once a frame.
 * @return true when a reload held during the build should run now
 */
bool PollGameBuild(EngineState* state, GameBuild& build);
}

#endif //WILL_ENGINE_GAME_BUILD_H
