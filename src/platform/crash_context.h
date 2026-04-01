//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_CRASH_CONTEXT_H
#define WILLENGINEV3_CRASH_CONTEXT_H

#include <string_view>

#include "core/containers/inline_string.h"

namespace Platform
{
class CrashContext
{
public:
    CrashContext();

    void WriteCrashContext(std::string_view crashReason, const char* folderPath);

private:
    Core::InlineString<32> sessionStart;

#ifdef _WIN32
    static void WriteSystemInfoWin32(FILE* f);
    static void WriteProcessInfoWin32(FILE* f);
#endif

    static Core::InlineString<32> GetTimestamp();
    static const char* GetBuildConfiguration();
};
} // Platform

#endif //WILLENGINEV3_CRASH_CONTEXT_H
