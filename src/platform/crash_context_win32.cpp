//
// Created by William on 2025-12-09.
//

#include "crash_context.h"

#include <cstdio>
#include <fstream>
#include <chrono>

#include <Windows.h>
#include <psapi.h>

#include <fmt/format.h>
#include <fmt/chrono.h>

namespace Platform
{
CrashContext::CrashContext()
{
    sessionStart = GetTimestamp();
}

void CrashContext::WriteCrashContext(std::string_view crashReason, const char* folderPath)
{
    char contextPath[1024];
    snprintf(contextPath, sizeof(contextPath), "%s/CrashContext.txt", folderPath);

    FILE* f = nullptr;
    fopen_s(&f, contextPath, "w");
    if (!f) {
        fmt::println("Failed to write crash context: could not open {}", contextPath);
        return;
    }

    fprintf(f, "session_start:  %s\n", sessionStart.c_str());
    fprintf(f, "build_config:   %s\n", GetBuildConfiguration());
    fprintf(f, "crash_time:     %s\n", GetTimestamp().c_str());
    fprintf(f, "crash_reason:   %.*s\n", (int) crashReason.size(), crashReason.data());
    fprintf(f, "\n[system]\n");
    WriteSystemInfoWin32(f);
    fprintf(f, "\n[process]\n");
    WriteProcessInfoWin32(f);

    fclose(f);

    fmt::println("Crash context written to: {}", contextPath);
}

void CrashContext::WriteSystemInfoWin32(FILE* f)
{
    MEMORYSTATUSEX memInfo = {};
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);

    SYSTEM_INFO sysInfo = {};
    GetSystemInfo(&sysInfo);

    fprintf(f, "total_memory_mb:     %llu\n", memInfo.ullTotalPhys / (1024 * 1024));
    fprintf(f, "available_memory_mb: %llu\n", memInfo.ullAvailPhys / (1024 * 1024));
    fprintf(f, "cpu_count:           %lu\n", sysInfo.dwNumberOfProcessors);
}

void CrashContext::WriteProcessInfoWin32(FILE* f)
{
    HANDLE process = GetCurrentProcess();

    PROCESS_MEMORY_COUNTERS memCounters = {};
    if (GetProcessMemoryInfo(process, &memCounters, sizeof(memCounters))) {
        fprintf(f, "working_set_mb:      %zu\n", memCounters.WorkingSetSize / (1024 * 1024));
        fprintf(f, "peak_working_set_mb: %zu\n", memCounters.PeakWorkingSetSize / (1024 * 1024));
    }

    DWORD handleCount = 0;
    GetProcessHandleCount(process, &handleCount);
    fprintf(f, "handle_count:        %lu\n", handleCount);
}

Core::InlineString<32> CrashContext::GetTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
    localtime_s(&tm, &time);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return Core::InlineString<32>(buf);
}

const char* CrashContext::GetBuildConfiguration()
{
#ifdef BUILD_CONFIG_NAME
    return BUILD_CONFIG_NAME;
#elif defined(PACKAGED_BUILD)
    return "Release";
#else
    return "Debug";
#endif
}
} // Platform
