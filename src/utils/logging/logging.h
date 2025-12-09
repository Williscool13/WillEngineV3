//
// Created by William on 2025-12-09.
//

#ifndef WILL_ENGINE_LOGGING_H
#define WILL_ENGINE_LOGGING_H

#include <memory>
#include <string>
#include <string_view>
#include <spdlog/spdlog.h>

namespace Utils
{
class Logger
{
public:
    explicit Logger(std::string_view logPath = "logs/engine.log");

    ~Logger();

    Logger(const Logger&) = delete;

    Logger& operator=(const Logger&) = delete;

    [[nodiscard]] std::shared_ptr<spdlog::logger> Get() const { return logger; }
    [[nodiscard]] std::string_view GetLogPath() const { return logPath; }

    void Flush();

private:
    std::shared_ptr<spdlog::logger> logger;
    std::string logPath;
};
} // Utils

#endif //WILL_ENGINE_LOGGING_H
