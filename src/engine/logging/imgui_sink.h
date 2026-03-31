#ifndef WILL_ENGINE_IMGUI_SINK_H
#define WILL_ENGINE_IMGUI_SINK_H

#include <algorithm>
#include <mutex>
#include <string>
#include <spdlog/sinks/base_sink.h>

#include "log_category.h"
#include "../../core/containers/ring_buffer.h"

namespace Engine
{
struct LogEntry
{
    uint64_t sequence;
    LogCategory category;
    spdlog::level::level_enum level;
    std::string message;
};

class ImGuiSink : public spdlog::sinks::base_sink<std::mutex>
{
    static constexpr size_t TRACE_CAPACITY    = 512;
    static constexpr size_t DEBUG_CAPACITY    = 256;
    static constexpr size_t INFO_CAPACITY     = 128;
    static constexpr size_t WARN_CAPACITY     = 64;
    static constexpr size_t ERROR_CAPACITY    = 32;
    static constexpr size_t CRITICAL_CAPACITY = 32;

public:
    void GetEntries(std::vector<LogEntry>& out) const
    {
        out.clear();
        auto append = [&](auto& buf) { buf.ForEach([&](const LogEntry& e) { out.push_back(e); }); };
        append(traceEntries);
        append(debugEntries);
        append(infoEntries);
        append(warnEntries);
        append(errorEntries);
        append(criticalEntries);
        std::sort(out.begin(), out.end(), [](const LogEntry& a, const LogEntry& b) { return a.sequence < b.sequence; });
    }

    void Clear()
    {
        std::lock_guard lock(mutex_);
        traceEntries.Clear();
        debugEntries.Clear();
        infoEntries.Clear();
        warnEntries.Clear();
        errorEntries.Clear();
        criticalEntries.Clear();
        nextSequence = 0;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        LogCategory cat = LogCategory::Temp;
        for (int i = 0; i < (int) LogCategory::Count; i++) {
            if (msg.logger_name == kCategoryNames[i]) {
                cat = static_cast<LogCategory>(i);
                break;
            }
        }

        LogEntry entry{nextSequence++, cat, msg.level, fmt::to_string(formatted)};

        switch (msg.level) {
            case spdlog::level::trace:    traceEntries.Push(std::move(entry));    break;
            case spdlog::level::debug:    debugEntries.Push(std::move(entry));    break;
            case spdlog::level::info:     infoEntries.Push(std::move(entry));     break;
            case spdlog::level::warn:     warnEntries.Push(std::move(entry));     break;
            case spdlog::level::err:      errorEntries.Push(std::move(entry));    break;
            case spdlog::level::critical: criticalEntries.Push(std::move(entry)); break;
            default: break;
        }
    }

    void flush_() override {}

private:
    uint64_t nextSequence = 0;
    Core::RingBuffer<LogEntry, TRACE_CAPACITY>    traceEntries;
    Core::RingBuffer<LogEntry, DEBUG_CAPACITY>    debugEntries;
    Core::RingBuffer<LogEntry, INFO_CAPACITY>     infoEntries;
    Core::RingBuffer<LogEntry, WARN_CAPACITY>     warnEntries;
    Core::RingBuffer<LogEntry, ERROR_CAPACITY>    errorEntries;
    Core::RingBuffer<LogEntry, CRITICAL_CAPACITY> criticalEntries;
};
} // Engine

#endif //WILL_ENGINE_IMGUI_SINK_H
