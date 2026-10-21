//
// Created by William on 2026-10-21.
//

#include "launch_args.h"

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "engine/engine_api.h"
#include "engine/console/console.h"
#include "engine/logging/engine_log.h"
#include "platform/file_utils.h"

namespace Engine::Args
{
static void AddParseError(LaunchArgs& out, const Core::InlineString<256>& message)
{
    if (!out.parseErrors.IsFull()) {
        out.parseErrors.PushBack(message);
    }
}

static void AddToken(LaunchArgs& out, std::string_view token)
{
    const int32_t tokenLen = static_cast<int32_t>(token.size());
    if (token.size() < 3 || token[0] != '-' || token[1] != '-') {
        AddParseError(out, Core::InlineString<256>::Format("ignoring '%.*s': arguments are --key or --key=value", tokenLen, token.data()));
        return;
    }
    const std::string_view body = token.substr(2);
    const size_t eq = body.find('=');
    const std::string_view name = body.substr(0, eq);
    if (name.empty() || name.size() >= sizeof(Core::ShortString::buf)) {
        AddParseError(out, Core::InlineString<256>::Format("ignoring '%.*s': bad key", tokenLen, token.data()));
        return;
    }
    const std::string_view value = eq == std::string_view::npos ? std::string_view{} : body.substr(eq + 1);
    if (value.size() >= sizeof(decltype(ParsedArg::value)::buf)) {
        AddParseError(out, Core::InlineString<256>::Format("ignoring --%.*s: value too long", static_cast<int32_t>(name.size()), name.data()));
        return;
    }
    if (out.parsed.IsFull()) {
        AddParseError(out, Core::InlineString<256>::Format("ignoring --%.*s: more than %zu arguments", static_cast<int32_t>(name.size()), name.data(), LaunchArgs::MAX_PARSED));
        return;
    }
    ParsedArg arg;
    arg.name = Core::ShortString(name);
    arg.value = Core::InlineString<512>(value);
    arg.bHasValue = eq != std::string_view::npos;
    out.parsed.PushBack(arg);
}

static void ParseResponseFile(LaunchArgs& out, const char* path)
{
    Platform::FileMapping file = Platform::MapFileReadOnly(Core::Path(path), true);
    if (file.data == nullptr) {
        AddParseError(out, Core::InlineString<256>::Format("could not read response file '%s'", path));
        return;
    }
    const char* p = reinterpret_cast<const char*>(file.data);
    const char* end = p + file.size;
    Core::InlineString<1024> token;
    while (p < end) {
        if (isspace(static_cast<unsigned char>(*p))) {
            ++p;
            continue;
        }
        if (*p == '#') {
            while (p < end && *p != '\n') { ++p; }
            continue;
        }
        token.Clear();
        bool bOverflow = false;
        while (p < end && !isspace(static_cast<unsigned char>(*p))) {
            if (*p == '"') {
                ++p;
                while (p < end && *p != '"') {
                    bOverflow |= !token.Append(p, 1);
                    ++p;
                }
                if (p < end) { ++p; }
                continue;
            }
            bOverflow |= !token.Append(p, 1);
            ++p;
        }
        if (bOverflow) {
            AddParseError(out, Core::InlineString<256>::Format("ignoring an over-long token in '%s'", path));
            continue;
        }
        AddToken(out, token.View());
    }
    Platform::UnmapFile(file);
}

void Parse(LaunchArgs& out, int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '@') {
            ParseResponseFile(out, argv[i] + 1);
        }
        else {
            AddToken(out, argv[i]);
        }
    }
}

static const RegisteredArg* FindRegistered(const LaunchArgs& args, const char* name)
{
    for (const RegisteredArg& reg : args.registered) {
        if (reg.name == name) { return &reg; }
    }
    return nullptr;
}

static const ParsedArg* FindLast(const LaunchArgs& args, const char* name)
{
    for (size_t i = args.parsed.Size(); i-- > 0;) {
        if (args.parsed[i].name == name) { return &args.parsed[i]; }
    }
    return nullptr;
}

static void CheckDeclared(const LaunchArgs& args, const ArgDesc& desc, ArgType readAs)
{
    [[maybe_unused]] const RegisteredArg* reg = FindRegistered(args, desc.name);
    assert(reg != nullptr && "launch arg read without being listed in its module's Register call");
    assert(reg->type == desc.type && desc.type == readAs && "launch arg read as the wrong type");
}

static bool ParseBool(const ParsedArg& arg, bool& out)
{
    if (!arg.bHasValue || arg.value == "true" || arg.value == "1") {
        out = true;
        return true;
    }
    if (arg.value == "false" || arg.value == "0") {
        out = false;
        return true;
    }
    return false;
}

static bool ParseInt(const ParsedArg& arg, int32_t& out)
{
    if (!arg.bHasValue || arg.value.IsEmpty()) { return false; }
    char* end = nullptr;
    errno = 0;
    const long long v = strtoll(arg.value.c_str(), &end, 10);
    if (*end != '\0' || errno != 0 || v < INT32_MIN || v > INT32_MAX) { return false; }
    out = static_cast<int32_t>(v);
    return true;
}

static bool ParseFloat(const ParsedArg& arg, float& out)
{
    if (!arg.bHasValue || arg.value.IsEmpty()) { return false; }
    char* end = nullptr;
    errno = 0;
    const float v = strtof(arg.value.c_str(), &end);
    if (*end != '\0' || errno != 0) { return false; }
    out = v;
    return true;
}

static Core::InlineString<64> DefaultText(const ArgDesc& desc)
{
    switch (desc.type) {
        case ArgType::Int: return Core::InlineString<64>::Format("%d", desc.intDefault);
        case ArgType::Float: return Core::InlineString<64>::Format("%g", desc.floatDefault);
        case ArgType::String: return Core::InlineString<64>(desc.stringDefault);
        default: return {};
    }
}

void Register(LaunchArgs& args, Origin origin, Core::Span<const ArgDesc* const> descs)
{
    for (size_t i = 0; i < descs.Size(); ++i) {
        const ArgDesc& desc = *descs[i];
        assert(FindRegistered(args, desc.name) == nullptr && "launch arg declared twice");
        assert(!args.registered.IsFull() && "launch arg overflow, raise LaunchArgs::MAX_REGISTERED");
        if (args.registered.IsFull()) { return; }
        RegisteredArg reg;
        reg.name = Core::ShortString(desc.name);
        reg.help = Core::InlineString<128>(desc.help);
        const Core::InlineString<64> fallback = DefaultText(desc);
        if (!fallback.IsEmpty()) {
            reg.help.Append(" (default ");
            reg.help.Append(fallback);
            reg.help.Append(")");
        }
        reg.type = desc.type;
        reg.origin = origin;
        args.registered.PushBack(reg);
    }
}

void ClearGameArgs(LaunchArgs& args)
{
    for (size_t i = args.registered.Size(); i-- > 0;) {
        if (args.registered[i].origin == Origin::Game) {
            args.registered.RemoveAt(i);
        }
    }
}

void Validate(const LaunchArgs& args)
{
    for (const auto& error : args.parseErrors) {
        LOG_WARN(Engine, "Launch args: {}", error.c_str());
    }
    for (const ParsedArg& arg : args.parsed) {
        const RegisteredArg* reg = FindRegistered(args, arg.name.c_str());
        if (reg == nullptr) {
            LOG_WARN(Engine, "Launch args: unknown --{}", arg.name.c_str());
            continue;
        }
        bool b;
        int32_t i;
        float f;
        switch (reg->type) {
            case ArgType::Bool:
                if (!ParseBool(arg, b)) { LOG_WARN(Engine, "Launch args: --{} takes true, false, 1 or 0, got '{}'", arg.name.c_str(), arg.value.c_str()); }
                break;
            case ArgType::Int:
                if (!ParseInt(arg, i)) { LOG_WARN(Engine, "Launch args: --{} takes an integer, got '{}'", arg.name.c_str(), arg.value.c_str()); }
                break;
            case ArgType::Float:
                if (!ParseFloat(arg, f)) { LOG_WARN(Engine, "Launch args: --{} takes a number, got '{}'", arg.name.c_str(), arg.value.c_str()); }
                break;
            case ArgType::String:
                if (!arg.bHasValue) { LOG_WARN(Engine, "Launch args: --{} needs a value, --{}=...", arg.name.c_str(), arg.name.c_str()); }
                break;
        }
    }
}

void LogHelp(const LaunchArgs& args)
{
    static constexpr const char* TYPE_SUFFIX[] = {"", "=<int>", "=<number>", "=<text>"};
    LOG_INFO(Engine, "Launch arguments (@file reads more from a file):");
    for (const RegisteredArg& reg : args.registered) {
        LOG_INFO(Engine, "  --{}{}  {}{}", reg.name.c_str(), TYPE_SUFFIX[static_cast<int32_t>(reg.type)], reg.help.c_str(), reg.origin == Origin::Game ? " [game]" : "");
    }
}

bool Has(const LaunchArgs& args, const ArgDesc& desc)
{
    CheckDeclared(args, desc, desc.type);
    return FindLast(args, desc.name) != nullptr;
}

bool GetBool(const LaunchArgs& args, const ArgDesc& desc)
{
    CheckDeclared(args, desc, ArgType::Bool);
    const ParsedArg* arg = FindLast(args, desc.name);
    bool v = false;
    return arg != nullptr && ParseBool(*arg, v) && v;
}

int32_t GetInt(const LaunchArgs& args, const ArgDesc& desc)
{
    CheckDeclared(args, desc, ArgType::Int);
    const ParsedArg* arg = FindLast(args, desc.name);
    int32_t v = desc.intDefault;
    if (arg == nullptr || !ParseInt(*arg, v)) { return desc.intDefault; }
    return v;
}

float GetFloat(const LaunchArgs& args, const ArgDesc& desc)
{
    CheckDeclared(args, desc, ArgType::Float);
    const ParsedArg* arg = FindLast(args, desc.name);
    float v = desc.floatDefault;
    if (arg == nullptr || !ParseFloat(*arg, v)) { return desc.floatDefault; }
    return v;
}

const char* GetString(const LaunchArgs& args, const ArgDesc& desc)
{
    CheckDeclared(args, desc, ArgType::String);
    const ParsedArg* arg = FindLast(args, desc.name);
    if (arg == nullptr || !arg->bHasValue) { return desc.stringDefault; }
    return arg->value.c_str();
}

uint32_t GetAll(const LaunchArgs& args, const ArgDesc& desc, Core::Span<const char*> out)
{
    CheckDeclared(args, desc, ArgType::String);
    uint32_t count = 0;
    for (const ParsedArg& arg : args.parsed) {
        if (count == out.Size()) { break; }
        if (arg.bHasValue && arg.name == desc.name) {
            out[count++] = arg.value.c_str();
        }
    }
    return count;
}

void RunLaunchExec(EngineContext* ctx, EngineState* state)
{
    LaunchArgs& args = *state->launchArgs;
    if (args.bExecDone || state->assetLoad.quietFrames < AssetLoadState::SETTLED_QUIET_FRAMES) { return; }
    args.bExecDone = true;

    const char* lines[LaunchArgs::MAX_PARSED];
    const uint32_t lineCount = GetAll(args, ARG_EXEC, Core::Span<const char*>(lines, LaunchArgs::MAX_PARSED));
    for (uint32_t i = 0; i < lineCount; ++i) {
        const char* p = lines[i];
        while (*p != '\0') {
            const char* start = p;
            while (*p != '\0' && *p != ';') { ++p; }
            std::string_view command(start, p - start);
            while (!command.empty() && isspace(static_cast<unsigned char>(command.front()))) { command.remove_prefix(1); }
            while (!command.empty() && isspace(static_cast<unsigned char>(command.back()))) { command.remove_suffix(1); }
            if (!command.empty()) {
                Console::ExecuteCommand(ctx, state, Core::InlineString<256>(command).c_str());
            }
            if (*p == ';') { ++p; }
        }
    }
}
}
