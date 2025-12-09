//
// Created by William on 2025-12-09.
//

#include "crash_context.h"

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
    context = nlohmann::ordered_json::object();
    context["session_start"] = GetTimestamp();
    context["build_config"] = GetBuildConfiguration();

    CollectSystemInfoWin32();
}

void CrashContext::WriteCrashContext(std::string_view crashReason, std::string_view folderPath)
{
    context["crash"]["reason"] = crashReason;
    context["crash"]["timestamp"] = GetTimestamp();

    CollectProcessInfoWin32();

    try {
        std::string contextPath = std::string(folderPath) + "CrashContext.json";
        std::ofstream file(contextPath);
        file << context.dump(2);
        file.close();

        fmt::println("Crash context written to: {}", contextPath);
    } catch (const std::exception& e) {
        fmt::println("Failed to write crash context: {}", e.what());
    }
}

void CrashContext::CollectSystemInfoWin32()
{
    // Memory info
    MEMORYSTATUSEX memInfo = {};
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);

    context["system"]["total_memory_mb"] = memInfo.ullTotalPhys / (1024 * 1024);
    context["system"]["available_memory_mb"] = memInfo.ullAvailPhys / (1024 * 1024);

    // CPU info
    SYSTEM_INFO sysInfo = {};
    GetSystemInfo(&sysInfo);
    context["system"]["cpu_count"] = sysInfo.dwNumberOfProcessors;
}

void CrashContext::CollectProcessInfoWin32()
{
    HANDLE process = GetCurrentProcess();

    // Process memory usage
    PROCESS_MEMORY_COUNTERS memCounters = {};
    if (GetProcessMemoryInfo(process, &memCounters, sizeof(memCounters))) {
        context["process"]["working_set_mb"] = memCounters.WorkingSetSize / (1024 * 1024);
        context["process"]["peak_working_set_mb"] = memCounters.PeakWorkingSetSize / (1024 * 1024);
    }

    // Handle count
    DWORD handleCount = 0;
    GetProcessHandleCount(process, &handleCount);
    context["process"]["handle_count"] = handleCount;
}

nlohmann::json CrashContext::GetBuildConfiguration()
{
#ifdef BUILD_CONFIG_NAME
    return BUILD_CONFIG_NAME;
#else
    #ifdef NDEBUG
        return "Release";
    #else
        return "Debug";
    #endif
#endif
}

std::string CrashContext::GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", now);
}
}