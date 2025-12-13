//
// Created by William on 2025-12-09.
//

#include "crash_handler.h"
#include "crash_context.h"

#include <filesystem>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <utility>

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

CrashHandler::CrashHandler(std::filesystem::path  dumpDirectory)
    : baseDumpDir(std::move(dumpDirectory))
{
    if (s_instance) {
        fmt::println("Warning: Multiple CrashHandler instances created");
    }

    s_instance = this;
    std::filesystem::create_directories(baseDumpDir);

    // #1 Setup exception filter
    SetUnhandledExceptionFilter(ExceptionFilter);

    fmt::println("Initialized crash handler: {}", baseDumpDir.string());
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

    std::filesystem::path currentCrashFolder = s_instance->CreateCrashFolder();

    fmt::println("Crash Detected. Writing to folder: {}", currentCrashFolder.string());

    // #2 Write crash context
    std::string crashReason = s_instance->GetExceptionDescription(pExceptionInfo);

    std::string stackTrace = GetStackTrace(pExceptionInfo->ContextRecord);

    try {
        if (const std::shared_ptr<spdlog::logger> defaultLogger = spdlog::default_logger()) {
            defaultLogger->error("{}", crashReason);
            defaultLogger->error("{}", stackTrace);
            defaultLogger->flush();
        }
    } catch (...) {
        fmt::println("Warning: Failed to log crash details via spdlog");
    }

    CrashContext context;
    context.WriteCrashContext(crashReason + stackTrace, currentCrashFolder);

    // #3 Copy Logs
    s_instance->CopyLogsToCrashes(currentCrashFolder);

    // #4 Write crash dump
    auto dumpPath = currentCrashFolder / "Minidump.dmp";
    if (s_instance->WriteDump(pExceptionInfo, dumpPath)) {
        fmt::println("Crash dump written to {}", dumpPath.string());
    }
    else {
        fmt::println("Failed to create dump");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

bool CrashHandler::WriteDump(PEXCEPTION_POINTERS pExceptionInfo, const std::filesystem::path& filename)
{
    HANDLE hFile = CreateFileA(
    filename.string().c_str(),
        GENERIC_WRITE,
        0, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) return false;

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
    std::filesystem::path currentCrashFolder = CreateCrashFolder();

    CopyLogsToCrashes(currentCrashFolder);

    CONTEXT context = {};
    RtlCaptureContext(&context);

    EXCEPTION_RECORD record = {};
    record.ExceptionCode = 0xC0000001; // Custom code for manual dump
    record.ExceptionAddress = RETURN_ADDRESS();

    EXCEPTION_POINTERS pointers = {};
    pointers.ExceptionRecord = &record;
    pointers.ContextRecord = &context;

    std::string fullReason = std::string("Manual dump: ") + std::string(reason);
    CrashContext crashContext;
    crashContext.WriteCrashContext(fullReason, currentCrashFolder);

    auto dumpPath = currentCrashFolder / "Minidump.dmp";
    return WriteDump(&pointers, dumpPath);
}

std::string CrashHandler::GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    return ss.str();
}

std::string CrashHandler::GetExceptionDescription(PEXCEPTION_POINTERS pExceptionInfo)
{
    DWORD exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
    void* exceptionAddress = pExceptionInfo->ExceptionRecord->ExceptionAddress;

    std::string description;

    switch (exceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
        {
            ULONG_PTR info0 = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR info1 = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];

            if (info0 == 0) {
                description = fmt::format("Access Violation: Read from invalid address 0x{:X}", info1);
            }
            else if (info0 == 1) {
                description = fmt::format("Access Violation: Write to invalid address 0x{:X}", info1);
            }
            else {
                description = "Access Violation: Execute at invalid address";
            }
            break;
        }
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            description = "Array bounds exceeded";
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            description = "Data type misalignment";
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            description = "Floating-point denormal operand";
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            description = "Floating-point division by zero";
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            description = "Floating-point inexact result";
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            description = "Floating-point invalid operation";
            break;
        case EXCEPTION_FLT_OVERFLOW:
            description = "Floating-point overflow";
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            description = "Floating-point stack check";
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            description = "Floating-point underflow";
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            description = "Illegal instruction";
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            description = "Page-in error";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            description = "Integer division by zero";
            break;
        case EXCEPTION_INT_OVERFLOW:
            description = "Integer overflow";
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            description = "Invalid exception disposition";
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            description = "Noncontinuable exception";
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            description = "Privileged instruction";
            break;
        case EXCEPTION_SINGLE_STEP:
            description = "Single step (debugger)";
            break;
        case EXCEPTION_STACK_OVERFLOW:
            description = "Stack overflow";
            break;
        case EXCEPTION_BREAKPOINT:
            description = "Breakpoint hit";
            break;
        default:
            description = fmt::format("Unknown exception (code: 0x{:X})", exceptionCode);
            break;
    }

    description += fmt::format(" at address 0x{:X}", reinterpret_cast<uintptr_t>(exceptionAddress));

    return description;
}

std::filesystem::path CrashHandler::CreateCrashFolder()
{
    std::string timestamp = GetTimestamp();
    std::filesystem::path crashFolder = baseDumpDir / timestamp;

    std::filesystem::create_directories(crashFolder);
    return crashFolder;
}

void CrashHandler::CopyLogsToCrashes(const std::filesystem::path& currentCrashFolder)
{
    try {
        auto defaultLogger = spdlog::default_logger();
        if (defaultLogger) {
            defaultLogger->flush();
        }

        if (logPath.empty() || !std::filesystem::exists(logPath)) {
            fmt::println("No log file to copy");
            return;
        }

        std::filesystem::path crashLogPath = currentCrashFolder / "engine.log";
        std::filesystem::copy_file(logPath, crashLogPath,
                                   std::filesystem::copy_options::overwrite_existing);

        fmt::println("Log file copied to: {}", crashLogPath.string());
    } catch (const std::exception& ex) {
        fmt::println("Failed to copy logs: {}", ex.what());
    }
}

std::string CrashHandler::GetStackTrace(PCONTEXT context)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    if (!SymInitialize(process, nullptr, TRUE)) {
        return "\nStack Trace: Failed to initialize symbol handler\n";
    }
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    std::stringstream ss;
    ss << "\nStack Trace:\n";

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

        // Symbols
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        auto symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            // Line
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;

            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line)) {
                ss << fmt::format("  #{} {} at {}:{}\n", frameNum, symbol->Name, line.FileName, line.LineNumber);
            }
            else {
                ss << fmt::format("  #{} {} + 0x{:X}\n", frameNum, symbol->Name, displacement);
            }
        }
        else {
            ss << fmt::format("  #{} 0x{:X}\n", frameNum, frame.AddrPC.Offset);
        }

        frameNum++;
        if (frameNum > 64) { break; }
    }

    SymCleanup(process);
    return ss.str();
}
} // Platform
