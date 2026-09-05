//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_MCP_CALL_INTERNAL_H
#define WILL_ENGINE_MCP_CALL_INTERNAL_H

#include "mcp_json.h"
#include "mcp_tool.h"
#include "core/containers/inline_string.h"

namespace Engine::MCP
{
static constexpr size_t CALL_RESULT_RESERVE = 1024;

/** The dispatcher opens and closes the root result object; handlers only ever write inside it. */
struct Call::Impl
{
    explicit Impl(Core::TlsfAllocator* allocator) : result(allocator, CALL_RESULT_RESERVE) {}

    const JsonReader* args{};
    int32_t argsToken{-1};
    JsonWriter result;
    Core::InlineString<256> errorMessage{};
    bool bError{false};
};
} // Engine::MCP

#endif //WILL_ENGINE_MCP_CALL_INTERNAL_H
