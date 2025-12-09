//
// Created by William on 2025-12-09.
//

#include "logging.h"

#include <filesystem>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/format.h>

namespace Utils
{
// Mapping: 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical
#ifndef LOG_LEVEL
#ifdef NDEBUG
#define LOG_LEVEL 2  // info
#else
#define LOG_LEVEL 0  // trace
#endif
#endif

Logger::Logger(std::string_view _logPath)
    : logPath(_logPath)
{
    const std::filesystem::path path(_logPath);
    std::filesystem::create_directories(path.parent_path());

    try {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string(_logPath), true);
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        consoleSink->set_pattern("[%^%l%$] %v");

        std::vector<spdlog::sink_ptr> sinks{fileSink, consoleSink};
        logger = std::make_shared<spdlog::logger>("engine", sinks.begin(), sinks.end());

        logger->set_level(static_cast<spdlog::level::level_enum>(LOG_LEVEL));
        logger->flush_on(spdlog::level::warn);

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);

        logger->info("Logger initialized: {}", logPath);
    } catch (const std::exception& ex) {
        fmt::print(stderr, "Failed to initialize logger: {}\n", ex.what());
    }
}

Logger::~Logger()
{
    if (logger) {
        logger->flush();
        spdlog::drop_all();
    }
}

void Logger::Flush()
{
    if (logger) {
        logger->flush();
    }
}
} // Utils
