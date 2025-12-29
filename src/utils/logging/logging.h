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

    [[nodiscard]] std::shared_ptr<spdlog::logger> Get() const { return logger; }
    [[nodiscard]] const std::filesystem::path& GetLogPath() const { return logPath; }

    void Flush();

private:
    std::shared_ptr<spdlog::logger> logger;
    std::filesystem::path logPath;
};
} // Utils

#endif //WILL_ENGINE_LOGGING_H
