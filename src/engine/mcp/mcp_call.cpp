//
// Created by William on 2026-09-05.
//

#include "mcp_call_internal.h"

#include <cassert>

namespace Engine::MCP
{
static int32_t FindArg(const Call::Impl& impl, const char* key)
{
    return impl.args ? impl.args->Find(impl.argsToken, key) : -1;
}

bool Call::HasArg(const char* key) const
{
    return FindArg(*impl, key) >= 0;
}

const char* Call::GetString(const char* key, const char* fallback) const
{
    return impl->args ? impl->args->GetString(FindArg(*impl, key), fallback) : fallback;
}

int64_t Call::GetInt(const char* key, const int64_t fallback) const
{
    double value = 0.0;
    return impl->args && impl->args->GetNumber(FindArg(*impl, key), value) ? static_cast<int64_t>(value) : fallback;
}

double Call::GetFloat(const char* key, const double fallback) const
{
    double value = 0.0;
    return impl->args && impl->args->GetNumber(FindArg(*impl, key), value) ? value : fallback;
}

bool Call::GetBool(const char* key, const bool fallback) const
{
    return impl->args ? impl->args->GetBool(FindArg(*impl, key), fallback) : fallback;
}

void Call::SetString(const char* key, const char* value)
{
    impl->result.Key(key);
    impl->result.String(value);
}

void Call::SetInt(const char* key, const int64_t value)
{
    impl->result.Key(key);
    impl->result.Int(value);
}

void Call::SetFloat(const char* key, const double value)
{
    impl->result.Key(key);
    impl->result.Number(value);
}

void Call::SetBool(const char* key, const bool value)
{
    impl->result.Key(key);
    impl->result.Bool(value);
}

void Call::SetNull(const char* key)
{
    impl->result.Key(key);
    impl->result.Null();
}

void Call::BeginObject(const char* key)
{
    impl->result.Key(key);
    impl->result.BeginObject();
}

void Call::BeginArray(const char* key)
{
    impl->result.Key(key);
    impl->result.BeginArray();
}

void Call::PushString(const char* value)
{
    impl->result.String(value);
}

void Call::PushInt(const int64_t value)
{
    impl->result.Int(value);
}

void Call::PushFloat(const double value)
{
    impl->result.Number(value);
}

void Call::PushBool(const bool value)
{
    impl->result.Bool(value);
}

void Call::PushObject()
{
    impl->result.BeginObject();
}

void Call::PushArray()
{
    impl->result.BeginArray();
}

void Call::End()
{
    assert(impl->result.Depth() > 1 && "Call::End: the root object belongs to the dispatcher");
    impl->result.End();
}

void Call::SetError(const char* message)
{
    impl->bError = true;
    impl->errorMessage = Core::InlineString<256>(std::string_view(message));
}
} // Engine::MCP
