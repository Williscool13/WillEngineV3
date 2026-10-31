//
// Created by William on 2026-10-21.
//

#include "launch_args.h"

#include "engine/engine_lifecycle.h"
#include "engine/will_engine.h"
#include "engine/editor/playtest_system.h"
#include "engine/mcp/mcp_server.h"

namespace Engine::Args
{
static constexpr const ArgDesc* ENGINE_ARGS[] = {
    &ARG_HELP,
    &ARG_EXEC,
    &ARG_SCENE,
    &ARG_PLAY,
    &ARG_OUT,
    &ARG_EXIT,
    &ARG_NO_REBAR,
    &ARG_UNATTENDED,
#if WILL_EDITOR
    &MCP::ARG_MCP_PORT,
#endif
};

void RegisterEngineArgs(LaunchArgs& args)
{
    Register(args, Origin::Engine, ENGINE_ARGS);
}
}
