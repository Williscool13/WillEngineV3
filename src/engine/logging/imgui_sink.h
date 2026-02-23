#ifndef WILL_ENGINE_IMGUI_SINK_H
#define WILL_ENGINE_IMGUI_SINK_H

#include <mutex>
#include <string>
#include <spdlog/sinks/base_sink.h>

#include "log_category.h"
#include "core/allocators/ring_buffer.h"

namespace Engine
{
struct LogEntry
{
    LogCategory category;
    std::string message;
    spdlog::level::level_enum level;
};

class ImGuiSink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    static constexpr size_t kMaxEntries = 1024;

    const Core::RingBuffer<LogEntry, kMaxEntries>& GetEntries() const { return entries; }

    void Clear()
    {
        std::lock_guard lock(mutex_);
        entries.Clear();
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
        entries.Push({cat, fmt::to_string(formatted), msg.level});
    }

    void flush_() override {}

private:
    Core::RingBuffer<LogEntry, kMaxEntries> entries;
};
} // Engine

#endif //WILL_ENGINE_IMGUI_SINK_H
