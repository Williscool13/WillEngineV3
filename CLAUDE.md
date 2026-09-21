# WillEngineV3

## Live engine access (MCP)

Editor builds run an MCP server on `http://127.0.0.1:8787/mcp` (`--mcp-port <n>` to change it). It is registered as `will-engine` in `.mcp.json`. When the editor is running, use these tools before asking the user to check something by hand.

- `get_engine_status` - readiness. Poll it after any mutation; `settled` means nothing has loaded for 30 frames.
- `get_frame_timings` - CPU and GPU frame timing, culling and pipeline counters. Safe to poll.
- `query_assets`, `query_scene`, `get_entity`, `find_entities` - what the engine knows about, and what is in the live scene.
- `spawn_entity` - a prefab, a model, or an empty entity, optionally parented.
- `exec_console_command`, `list_console_commands` - the in-game developer console, with printed output returned.
- `capture_screenshot` - returns a PNG path immediately; `frames` > 1 captures consecutive render frames as `<stem>_NNN.png`. Poll `get_engine_status.screenshotInFlight` until false, then read the files.
- `pick_pixel` - what is under a viewport pixel. Arm it with `{u, v}` (0..1, top-left origin, the image `capture_screenshot` saves), then call it with no args for the entity, primitive, material and emissive-light state; `resolved` is false until the GPU answers.
- `get_log_info` - where `engine.log` is and how to read it. The log is the return channel for side effects: every engine-thread call is bracketed by `mcp/<callId> begin` and `end` lines.

Ids on the wire are 16 hex digits. If a question needs a tool that does not exist, propose the tool rather than working around it; a tool is one handler plus one registration line in `src/engine/mcp/mcp_tools_engine.cpp` (engine data) or `src/game/mcp/game_mcp_tools.cpp` (game types).
