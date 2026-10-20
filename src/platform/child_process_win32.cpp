//
// Created by William on 2026-10-19.
//

#include <Windows.h>

#include <algorithm>
#include <cstring>

#include "child_process.h"
#include "spdlog/spdlog.h"

namespace Platform
{
bool ChildProcess::Start(const char* commandLine, const char* environment)
{
    Close();

    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 64 * 1024)) {
        SPDLOG_ERROR("ChildProcess: CreatePipe failed ({})", GetLastError());
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;

    // CreateProcessA may write into the command line
    char mutableCommand[MAX_COMMAND_LINE];
    const size_t length = strlen(commandLine);
    if (length >= MAX_COMMAND_LINE) {
        SPDLOG_ERROR("ChildProcess: command line longer than {} characters", MAX_COMMAND_LINE - 1);
        CloseHandle(readEnd);
        CloseHandle(writeEnd);
        if (nul != INVALID_HANDLE_VALUE) { CloseHandle(nul); }
        return false;
    }
    memcpy(mutableCommand, commandLine, length + 1);

    PROCESS_INFORMATION info{};
    const BOOL bCreated = CreateProcessA(nullptr, mutableCommand, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, const_cast<char*>(environment), nullptr, &startup, &info);
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE) { CloseHandle(nul); }
    if (!bCreated) {
        SPDLOG_ERROR("ChildProcess: CreateProcess failed ({}) for: {}", GetLastError(), commandLine);
        CloseHandle(readEnd);
        return false;
    }

    CloseHandle(info.hThread);
    process = info.hProcess;
    outputRead = readEnd;
    return true;
}

void ChildProcess::ReadOutput(Core::Vector<char>& out)
{
    if (!outputRead) { return; }
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr) || available == 0) { return; }
        char chunk[4096];
        DWORD read = 0;
        if (!ReadFile(outputRead, chunk, std::min<DWORD>(available, sizeof(chunk)), &read, nullptr) || read == 0) { return; }
        const size_t offset = out.Size();
        out.Resize(offset + read);
        memcpy(out.Data() + offset, chunk, read);
    }
}

bool ChildProcess::IsRunning() const
{
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

uint32_t ChildProcess::ExitCode() const
{
    DWORD code = 0;
    if (!process || !GetExitCodeProcess(process, &code)) { return ~0u; }
    return code;
}

void ChildProcess::Close()
{
    if (outputRead) { CloseHandle(outputRead); }
    if (process) { CloseHandle(process); }
    outputRead = nullptr;
    process = nullptr;
}
}
