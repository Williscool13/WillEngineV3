//
// Created by William on 2025-12-09.
//

#include "crash_handler.h"
#include "crash_context.h"
#include "file_utils.h"

#include <dbghelp.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#ifdef _MSC_VER
#include <intrin.h>
#define RETURN_ADDRESS() _ReturnAddress()
#else
#define RETURN_ADDRESS() __builtin_return_address(0)
#endif

namespace Platform
{
CrashHandler* CrashHandler::s_instance = nullptr;


static size_t BufAppend(char* buf, size_t cap, size_t pos, const char* str)
{
    if (pos >= cap - 1) { return pos; }
    size_t avail = cap - 1 - pos;
    size_t slen = strlen(str);
    if (slen > avail) { slen = avail; }
    memcpy(buf + pos, str, slen);
    buf[pos + slen] = '\0';
    return pos + slen;
}

static size_t BufAppendf(char* buf, size_t cap, size_t pos, const char* fmt, ...)
{
    if (pos >= cap - 1) { return pos; }
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + pos, cap - pos, fmt, args);
    va_end(args);
    if (n <= 0) { return pos; }
    pos += (size_t) n;
    if (pos >= cap) { pos = cap - 1; }
    return pos;
}

CrashHandler::CrashHandler(const char* dumpDirectory)
    : baseDumpDir(dumpDirectory)
{
    if (s_instance) {
        fmt::println("Warning: Multiple CrashHandler instances created");
    }

    s_instance = this;
    CreateDirectories(baseDumpDir.c_str());

    SetUnhandledExceptionFilter(ExceptionFilter);

    fmt::println("Initialized crash handler: {}", baseDumpDir.c_str());
}

CrashHandler::~CrashHandler()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
    SetUnhandledExceptionFilter(nullptr);
}

LONG WINAPI CrashHandler::ExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo)
{
    if (!s_instance) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    Core::Path currentCrashFolder = s_instance->CreateCrashFolder();

    fmt::println("Crash Detected. Writing to folder: {}", currentCrashFolder.c_str());

    auto crashReason = GetExceptionDescription(pExceptionInfo);
    auto stackTrace = GetStackTrace(pExceptionInfo->ContextRecord);

    auto defaultLogger = spdlog::default_logger();
    if (defaultLogger) {
        defaultLogger->error("{}", crashReason.c_str());
        defaultLogger->error("{}", stackTrace.c_str());
        defaultLogger->flush();
    }

    char combined[256 + 8192];
    size_t pos = 0;
    pos = BufAppend(combined, sizeof(combined), pos, crashReason.c_str());
    pos = BufAppend(combined, sizeof(combined), pos, stackTrace.c_str());

    CrashContext context;
    context.WriteCrashContext(combined, currentCrashFolder.c_str());

    s_instance->CopyLogsToCrashes(currentCrashFolder);

    Core::Path dumpPath = currentCrashFolder / "Minidump.dmp";
    if (s_instance->WriteDump(pExceptionInfo, dumpPath)) {
        fmt::println("Crash dump written to {}", dumpPath.c_str());
    }
    else {
        fmt::println("Failed to create dump");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

bool CrashHandler::WriteDump(PEXCEPTION_POINTERS pExceptionInfo, const Core::Path& filename)
{
    HANDLE hFile = CreateFileA(
        filename.c_str(),
        GENERIC_WRITE,
        0, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) { return false; }

    MINIDUMP_EXCEPTION_INFORMATION mdei = {};
    mdei.ThreadId = GetCurrentThreadId();
    mdei.ExceptionPointers = pExceptionInfo;
    mdei.ClientPointers = FALSE;

    BOOL success = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        hFile,
        MiniDumpWithDataSegs,
        &mdei,
        nullptr,
        nullptr
    );

    CloseHandle(hFile);
    return success != FALSE;
}

bool CrashHandler::TriggerManualDump(std::string_view reason)
{
    Core::Path currentCrashFolder = CreateCrashFolder();

    CopyLogsToCrashes(currentCrashFolder);

    CONTEXT ctx = {};
    RtlCaptureContext(&ctx);

    EXCEPTION_RECORD record = {};
    record.ExceptionCode = 0xC0000001;
    record.ExceptionAddress = RETURN_ADDRESS();

    EXCEPTION_POINTERS pointers = {};
    pointers.ExceptionRecord = &record;
    pointers.ContextRecord = &ctx;

    char fullReason[512];
    snprintf(fullReason, sizeof(fullReason), "Manual dump: %.*s", (int) reason.size(), reason.data());

    CrashContext crashContext;
    crashContext.WriteCrashContext(fullReason, currentCrashFolder.c_str());

    Core::Path dumpPath = currentCrashFolder / "Minidump.dmp";
    return WriteDump(&pointers, dumpPath);
}

Core::InlineString<32> CrashHandler::GetTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    return Core::InlineString<32>(buf);
}

Core::InlineString<256> CrashHandler::GetExceptionDescription(PEXCEPTION_POINTERS pExceptionInfo)
{
    DWORD exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
    void* exceptionAddress = pExceptionInfo->ExceptionRecord->ExceptionAddress;

    char buf[256];
    size_t pos = 0;

    switch (exceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
        {
            ULONG_PTR info0 = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR info1 = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];
            if (info0 == 0) {
                pos = BufAppendf(buf, sizeof(buf), pos, "Access Violation: Read from invalid address 0x%llX", (unsigned long long) info1);
            }
            else if (info0 == 1) {
                pos = BufAppendf(buf, sizeof(buf), pos, "Access Violation: Write to invalid address 0x%llX", (unsigned long long) info1);
            }
            else {
                pos = BufAppend(buf, sizeof(buf), pos, "Access Violation: Execute at invalid address");
            }
            break;
        }
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: pos = BufAppend(buf, sizeof(buf), pos, "Array bounds exceeded");
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT: pos = BufAppend(buf, sizeof(buf), pos, "Data type misalignment");
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND: pos = BufAppend(buf, sizeof(buf), pos, "Floating-point denormal operand");
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: pos = BufAppend(buf, sizeof(buf), pos, "Floating-point division by zero");
            break;
        case EXCEPTION_FLT_INEXACT_RESULT: pos = BufAppend(buf, sizeof(buf), pos, "Floating-point inexact result");
            break;
        case EXCEPTION_FLT_INVALID_OPERATION: pos = BufAppend(buf, sizeof(buf), pos, "Floating-point invalid operation");
            break;
        case EXCEPTION_FLT_OVERFLOW: pos = BufAppend(buf, sizeof(buf), pos, "Floating-point overflow");
            break;
        case EXCEPTION_FLT_STACK_CHECK: pos = BufAppend(buf, sizeof(buf), pos, "Floating-point stack check");
            break;
        case EXCEPTION_FLT_UNDERFLOW: pos = BufAppend(buf, sizeof(buf), pos, "Floating-point underflow");
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION: pos = BufAppend(buf, sizeof(buf), pos, "Illegal instruction");
            break;
        case EXCEPTION_IN_PAGE_ERROR: pos = BufAppend(buf, sizeof(buf), pos, "Page-in error");
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO: pos = BufAppend(buf, sizeof(buf), pos, "Integer division by zero");
            break;
        case EXCEPTION_INT_OVERFLOW: pos = BufAppend(buf, sizeof(buf), pos, "Integer overflow");
            break;
        case EXCEPTION_INVALID_DISPOSITION: pos = BufAppend(buf, sizeof(buf), pos, "Invalid exception disposition");
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: pos = BufAppend(buf, sizeof(buf), pos, "Noncontinuable exception");
            break;
        case EXCEPTION_PRIV_INSTRUCTION: pos = BufAppend(buf, sizeof(buf), pos, "Privileged instruction");
            break;
        case EXCEPTION_SINGLE_STEP: pos = BufAppend(buf, sizeof(buf), pos, "Single step (debugger)");
            break;
        case EXCEPTION_STACK_OVERFLOW: pos = BufAppend(buf, sizeof(buf), pos, "Stack overflow");
            break;
        case EXCEPTION_BREAKPOINT: pos = BufAppend(buf, sizeof(buf), pos, "Breakpoint hit");
            break;
        default:
            pos = BufAppendf(buf, sizeof(buf), pos, "Unknown exception (code: 0x%lX)", exceptionCode);
            break;
    }

    BufAppendf(buf, sizeof(buf), pos, " at address 0x%llX", (unsigned long long) (uintptr_t) exceptionAddress);
    return Core::InlineString<256>(buf);
}

Core::Path CrashHandler::CreateCrashFolder()
{
    auto timestamp = GetTimestamp();
    Core::Path crashFolder = baseDumpDir / timestamp.c_str();
    CreateDirectoryA(crashFolder.c_str(), nullptr);
    return crashFolder;
}

void CrashHandler::CopyLogsToCrashes(const Core::Path& currentCrashFolder)
{
    if (logPath.IsEmpty() || !logPath.Exists()) {
        fmt::println("No log file to copy");
        return;
    }

    Core::Path crashLogPath = currentCrashFolder / "engine.log";
    if (!Platform::FileCopy(logPath.c_str(), crashLogPath.c_str())) {
        fmt::println("Failed to copy logs");
        return;
    }

    fmt::println("Log file copied to: {}", crashLogPath.c_str());
}

Core::InlineString<8192> CrashHandler::GetStackTrace(PCONTEXT context)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // ERROR_INVALID_PARAMETER = dbghelp already initialized for this process (e.g. by Tracy)
    if (!SymInitialize(process, nullptr, TRUE) && GetLastError() != ERROR_INVALID_PARAMETER) {
        return Core::InlineString<8192>("\nStack Trace: Failed to initialize symbol handler\n");
    }
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    char buf[8192];
    size_t pos = 0;
    pos = BufAppend(buf, sizeof(buf), pos, "\nStack Trace:\n");

    int frameNum = 0;
    while (StackWalk64(
        IMAGE_FILE_MACHINE_AMD64,
        process,
        thread,
        &frame,
        context,
        nullptr,
        SymFunctionTableAccess64,
        SymGetModuleBase64,
        nullptr)) {
        if (frame.AddrPC.Offset == 0) { break; }

        char symbolBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        auto symbol = reinterpret_cast<PSYMBOL_INFO>(symbolBuf);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;

            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line)) {
                pos = BufAppendf(buf, sizeof(buf), pos, "  #%d %s at %s:%lu\n", frameNum, symbol->Name, line.FileName, line.LineNumber);
            }
            else {
                pos = BufAppendf(buf, sizeof(buf), pos, "  #%d %s + 0x%llX\n", frameNum, symbol->Name, (unsigned long long) displacement);
            }
        }
        else {
            pos = BufAppendf(buf, sizeof(buf), pos, "  #%d 0x%llX\n", frameNum, (unsigned long long) frame.AddrPC.Offset);
        }

        ++frameNum;
        if (frameNum > 64) { break; }
    }

    SymCleanup(process);
    return Core::InlineString<8192>(buf);
}
} // Platform
