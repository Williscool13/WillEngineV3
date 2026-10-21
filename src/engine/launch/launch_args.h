//
// Created by William on 2026-10-21.
//

#ifndef WILL_ENGINE_LAUNCH_ARGS_H
#define WILL_ENGINE_LAUNCH_ARGS_H

#include <cstdint>

#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/containers/span.h"
#include "engine/core/origin.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Engine::Args
{
enum class ArgType : uint8_t { Bool, Int, Float, String };

/** Declared next to the system that reads it; each module lists its descs once through Register. */
struct ArgDesc
{
    const char* name{};
    ArgType type{ArgType::Bool};
    const char* help{};
    int32_t intDefault{0};
    float floatDefault{0.0f};
    const char* stringDefault{""};
};

constexpr ArgDesc BoolArg(const char* name, const char* help) { return {name, ArgType::Bool, help}; }
constexpr ArgDesc IntArg(const char* name, int32_t fallback, const char* help) { return {name, ArgType::Int, help, fallback}; }
constexpr ArgDesc FloatArg(const char* name, float fallback, const char* help) { return {name, ArgType::Float, help, 0, fallback}; }
constexpr ArgDesc StringArg(const char* name, const char* fallback, const char* help) { return {name, ArgType::String, help, 0, 0.0f, fallback}; }

struct ParsedArg
{
    Core::ShortString name{};
    Core::InlineString<512> value{};
    bool bHasValue{false};
};

struct RegisteredArg
{
    Core::ShortString name{};
    Core::InlineString<128> help{};
    ArgType type{ArgType::Bool};
    Origin origin{Origin::Engine};
};

/** Owned by the exe for the whole run; EngineState points at it. */
struct LaunchArgs
{
    static constexpr size_t MAX_PARSED = 64;
    static constexpr size_t MAX_REGISTERED = 128;
    static constexpr size_t MAX_PARSE_ERRORS = 16;

    Core::InlineVector<ParsedArg, MAX_PARSED> parsed{};
    Core::InlineVector<RegisteredArg, MAX_REGISTERED> registered{};
    /** Reported by Validate, since Parse runs before the engine loggers exist. */
    Core::InlineVector<Core::InlineString<256>, MAX_PARSE_ERRORS> parseErrors{};
    bool bExecDone{false};
};

inline constexpr ArgDesc ARG_HELP = BoolArg("help", "list every argument and quit");
inline constexpr ArgDesc ARG_EXEC = StringArg("exec", "", "console commands separated by ';', run once the startup scene settles; repeatable");

/**
 * Accepts `--key` and `--key=value`; `@file` splices whitespace-separated tokens from a file (double quotes group, # comments to end of line).
 * A repeated key keeps every occurrence; single-value getters read the last.
 */
void Parse(LaunchArgs& out, int argc, char** argv);

void Register(LaunchArgs& args, Origin origin, Core::Span<const ArgDesc* const> descs);

void RegisterEngineArgs(LaunchArgs& args);

void ClearGameArgs(LaunchArgs& args);

/** Warns about parse errors, undeclared keys and values that do not fit their declared type. */
void Validate(const LaunchArgs& args);

void LogHelp(const LaunchArgs& args);

bool Has(const LaunchArgs& args, const ArgDesc& desc);

bool GetBool(const LaunchArgs& args, const ArgDesc& desc);

int32_t GetInt(const LaunchArgs& args, const ArgDesc& desc);

float GetFloat(const LaunchArgs& args, const ArgDesc& desc);

const char* GetString(const LaunchArgs& args, const ArgDesc& desc);

/** @return the number of occurrences written to out, in command-line order. */
uint32_t GetAll(const LaunchArgs& args, const ArgDesc& desc, Core::Span<const char*> out);

/** Runs every --exec once, the first frame the startup scene has settled. */
void RunLaunchExec(EngineContext* ctx, EngineState* state);
}

#endif //WILL_ENGINE_LAUNCH_ARGS_H
