//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_MCP_CALL_INTERNAL_H
#define WILL_ENGINE_MCP_CALL_INTERNAL_H

#include <nlohmann/json.hpp>

#include "mcp_tool.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"

namespace Engine::MCP
{
static constexpr size_t CALL_MAX_NESTING = 16;

struct Call::Impl
{
    const nlohmann::json* args{};
    nlohmann::json result = nlohmann::json::object();
    Core::InlineVector<nlohmann::json*, CALL_MAX_NESTING> stack{};
    Core::InlineString<256> errorMessage{};
    bool bError{false};
};
} // Engine::MCP

#endif //WILL_ENGINE_MCP_CALL_INTERNAL_H
