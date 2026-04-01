//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_CRASH_HANDLER_H
#define WILLENGINEV3_CRASH_HANDLER_H
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"

namespace Platform
{
class CrashHandler
{
public:
    explicit CrashHandler(const char* dumpDirectory);

    ~CrashHandler();

    CrashHandler(const CrashHandler&) = delete;

    CrashHandler& operator=(const CrashHandler&) = delete;

    bool TriggerManualDump(std::string_view reason);

    void SetLogPath(const char* path) { logPath = Core::Path(path); }

private:
    Core::Path baseDumpDir;
    Core::Path logPath;

#ifdef _WIN32
    static LONG WINAPI ExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo);

    static CrashHandler* s_instance;

    bool WriteDump(PEXCEPTION_POINTERS pExceptionInfo, const Core::Path& filename);

    static Core::InlineString<256>   GetExceptionDescription(PEXCEPTION_POINTERS pExceptionInfo);

    Core::Path CreateCrashFolder();

    void CopyLogsToCrashes(const Core::Path& currentCrashFolder);

    static Core::InlineString<8192>  GetStackTrace(PCONTEXT context);
#endif

    static Core::InlineString<32> GetTimestamp();
};
}

#endif //WILLENGINEV3_CRASH_HANDLER_H
