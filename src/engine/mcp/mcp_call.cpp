//
// Created by William on 2026-09-05.
//

#include "mcp_call_internal.h"

#include <cassert>

namespace Engine::MCP
{
static const nlohmann::json* FindArg(const Call::Impl& impl, const char* key)
{
    if (!impl.args || !impl.args->is_object()) {
        return nullptr;
    }
    const auto it = impl.args->find(key);
    return it == impl.args->end() ? nullptr : &*it;
}

static nlohmann::json& Top(Call::Impl& impl)
{
    return impl.stack.IsEmpty() ? impl.result : *impl.stack.Back();
}

static void Open(Call::Impl& impl, nlohmann::json& container)
{
    assert(!impl.stack.IsFull() && "MCP Call nesting exceeds CALL_MAX_NESTING");
    impl.stack.PushBack(&container);
}

bool Call::HasArg(const char* key) const
{
    return FindArg(*impl, key) != nullptr;
}

const char* Call::GetString(const char* key, const char* fallback) const
{
    const nlohmann::json* v = FindArg(*impl, key);
    return v && v->is_string() ? v->get_ref<const std::string&>().c_str() : fallback;
}

int64_t Call::GetInt(const char* key, const int64_t fallback) const
{
    const nlohmann::json* v = FindArg(*impl, key);
    return v && v->is_number() ? v->get<int64_t>() : fallback;
}

double Call::GetFloat(const char* key, const double fallback) const
{
    const nlohmann::json* v = FindArg(*impl, key);
    return v && v->is_number() ? v->get<double>() : fallback;
}

bool Call::GetBool(const char* key, const bool fallback) const
{
    const nlohmann::json* v = FindArg(*impl, key);
    return v && v->is_boolean() ? v->get<bool>() : fallback;
}

void Call::SetString(const char* key, const char* value)
{
    Top(*impl)[key] = value ? value : "";
}

void Call::SetInt(const char* key, const int64_t value)
{
    Top(*impl)[key] = value;
}

void Call::SetFloat(const char* key, const double value)
{
    Top(*impl)[key] = value;
}

void Call::SetBool(const char* key, const bool value)
{
    Top(*impl)[key] = value;
}

void Call::SetNull(const char* key)
{
    Top(*impl)[key] = nullptr;
}

void Call::BeginObject(const char* key)
{
    nlohmann::json& slot = Top(*impl)[key];
    slot = nlohmann::json::object();
    Open(*impl, slot);
}

void Call::BeginArray(const char* key)
{
    nlohmann::json& slot = Top(*impl)[key];
    slot = nlohmann::json::array();
    Open(*impl, slot);
}

void Call::PushString(const char* value)
{
    Top(*impl).push_back(value ? value : "");
}

void Call::PushInt(const int64_t value)
{
    Top(*impl).push_back(value);
}

void Call::PushFloat(const double value)
{
    Top(*impl).push_back(value);
}

void Call::PushBool(const bool value)
{
    Top(*impl).push_back(value);
}

void Call::PushObject()
{
    nlohmann::json& parent = Top(*impl);
    parent.push_back(nlohmann::json::object());
    Open(*impl, parent.back());
}

void Call::PushArray()
{
    nlohmann::json& parent = Top(*impl);
    parent.push_back(nlohmann::json::array());
    Open(*impl, parent.back());
}

void Call::End()
{
    assert(!impl->stack.IsEmpty() && "MCP Call End() without a matching Begin/Push");
    impl->stack.PopBack();
}

void Call::SetError(const char* message)
{
    impl->bError = true;
    impl->errorMessage = Core::InlineString<256>(message ? message : "");
}
} // Engine::MCP
