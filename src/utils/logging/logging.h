//
// Created by William on 2025-12-09.
//

#ifndef WILL_ENGINE_LOGGING_H
#define WILL_ENGINE_LOGGING_H

#include <filesystem>
#include <memory>
#include <spdlog/spdlog.h>

namespace Utils
{
class Logger
{
public:
    explicit Logger(const std::filesystem::path& _logPath);

    ~Logger();

    void ArchiveLogs();

    Logger(const Logger&) = delete;

    Logger& operator=(const Logger&) = delete;

    void AddSink(spdlog::sink_ptr sink);

    [[nodiscard]] const std::filesystem::path& GetLogPath() const { return logPath; }

    const std::vector<spdlog::sink_ptr>& GetSinks() const { return sinks; }

private:
    std::filesystem::path logPath;
    std::vector<spdlog::sink_ptr> sinks;
};
} // Utils

#endif //WILL_ENGINE_LOGGING_H
