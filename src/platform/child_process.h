//
// Created by William on 2026-10-19.
//

#ifndef WILL_ENGINE_CHILD_PROCESS_H
#define WILL_ENGINE_CHILD_PROCESS_H

#include <cstdint>

#include "core/containers/vector.h"

namespace Platform
{
/** A windowless child process whose stdout and stderr share one pipe, read without blocking. */
class ChildProcess
{
public:
    static constexpr size_t MAX_COMMAND_LINE = 4096;

    ChildProcess() = default;
    ~ChildProcess() { Close(); }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    /** environment: "KEY=VALUE\0...\0\0", or null to inherit this process's. */
    bool Start(const char* commandLine, const char* environment = nullptr);

    void ReadOutput(Core::Vector<char>& out);

    bool IsRunning() const;

    uint32_t ExitCode() const;

    bool IsStarted() const { return process != nullptr; }

    void Close();

private:
    void* process{};
    void* outputRead{};
};
}

#endif //WILL_ENGINE_CHILD_PROCESS_H
