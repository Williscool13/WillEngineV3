//
// Created by William on 2025-12-09.
//

#include "logging.h"

#include "core/time/frame_stamp.h"
#include "platform/file_utils.h"

#include <chrono>
#include <ctime>
#include <iterator>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/format.h>

namespace Utils
{
class FrameStampFlag final : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg&, const std::tm&, spdlog::memory_buf_t& dest) override
    {
        fmt::format_to(std::back_inserter(dest), "{:>6}|{:>6}", Core::gGameFrame.load(std::memory_order_relaxed), Core::gRenderFrame.load(std::memory_order_relaxed));
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override { return spdlog::details::make_unique<FrameStampFlag>(); }
};

Logger::Logger(const Core::Path& _logPath)
    : logPath(_logPath)
{
    Platform::CreateDirectories(_logPath.Parent().c_str());

    try {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.c_str(), true);
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        auto fileFormatter = std::make_unique<spdlog::pattern_formatter>();
        fileFormatter->add_flag<FrameStampFlag>('N');
        fileFormatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%N] [%^%l%$] [%t] [%n] [%!] %v");
        fileSink->set_formatter(std::move(fileFormatter));

        consoleSink->set_pattern("[%^%l%$] [%n] %v");

        sinks.PushBack(fileSink);
        sinks.PushBack(consoleSink);
    } catch (const std::exception& ex) {
        fmt::print(stderr, "Failed to initialize logger: {}\n", ex.what());
    }
}

Logger::~Logger() = default;

void Logger::ArchiveLogs()
{
    if (!logPath.Exists()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;

#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    char archiveFilename[128];
    std::strftime(archiveFilename, sizeof(archiveFilename), "engine_%Y%m%d_%H%M%S.log", &tm);
    Core::Path archivePath = logPath.Parent() / archiveFilename;

    Platform::FileCopy(logPath.c_str(), archivePath.c_str());
}

void Logger::AddSink(spdlog::sink_ptr sink)
{
    sinks.PushBack(sink);
}
} // Utils
