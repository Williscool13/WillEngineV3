//
// Created by William on 2025-12-09.
//

#ifndef WILL_ENGINE_LOGGING_H
#define WILL_ENGINE_LOGGING_H

#include <memory>
#include <spdlog/spdlog.h>

#include "core/containers/inline_path.h"
#include "core/containers/inline_vector.h"

namespace Utils
{
class Logger
{
public:
    explicit Logger(const Core::Path& _logPath);

    ~Logger();

    void ArchiveLogs();

    Logger(const Logger&) = delete;

    Logger& operator=(const Logger&) = delete;

    void AddSink(spdlog::sink_ptr sink);

    [[nodiscard]] const Core::Path& GetLogPath() const { return logPath; }

    [[nodiscard]] const Core::InlineVector<spdlog::sink_ptr, 4>& GetSinks() const { return sinks; }

private:
    Core::Path logPath;
    Core::InlineVector<spdlog::sink_ptr, 4> sinks;
};
} // Utils

#endif //WILL_ENGINE_LOGGING_H
