//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_CRASH_HANDLER_H
#define WILLENGINEV3_CRASH_HANDLER_H
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Platform
{
class CrashHandler
{
public:
    explicit CrashHandler(std::filesystem::path  dumpDirectory);

    ~CrashHandler();

    CrashHandler(const CrashHandler&) = delete;

    CrashHandler& operator=(const CrashHandler&) = delete;

    bool TriggerManualDump(std::string_view reason);

    void SetLogPath(const std::filesystem::path& path) { logPath = path; }

private:
    std::filesystem::path baseDumpDir;
    std::filesystem::path logPath;

#ifdef _WIN32
    static LONG WINAPI ExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo);

    static CrashHandler* s_instance;

    bool WriteDump(PEXCEPTION_POINTERS pExceptionInfo, const std::filesystem::path& filename);

    std::string GetExceptionDescription(PEXCEPTION_POINTERS pExceptionInfo);

    std::filesystem::path CreateCrashFolder();

    void CopyLogsToCrashes(const std::filesystem::path& currentCrashFolder);

    static std::string GetStackTrace(PCONTEXT context);
#endif

    static std::string GetTimestamp();
};
}

#endif //WILLENGINEV3_CRASH_HANDLER_H
