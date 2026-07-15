#ifndef WILL_ENGINE_IMGUI_SINK_H
#define WILL_ENGINE_IMGUI_SINK_H

#include <algorithm>
#include <cassert>
#include <mutex>
#include <spdlog/sinks/base_sink.h>

#include "log_category.h"
#include "core/containers/fixed_vector.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/containers/ring_buffer.h"
#include "core/containers/span.h"

namespace Engine
{
struct LogEntry
{
    uint64_t sequence;
    LogCategory category;
    /** Index into ImGuiSink's game sub-category registry when category == Game */
    int8_t gameSubCategory{-1};
    spdlog::level::level_enum level;
    Core::InlineString<256> message;
};

class ImGuiSink : public spdlog::sinks::base_sink<std::mutex>
{
    static constexpr size_t TRACE_CAPACITY    = 512;
    static constexpr size_t DEBUG_CAPACITY    = 256;
    static constexpr size_t INFO_CAPACITY     = 128;
    static constexpr size_t WARN_CAPACITY     = 64;
    static constexpr size_t ERROR_CAPACITY    = 32;
    static constexpr size_t CRITICAL_CAPACITY = 32;
    static constexpr size_t TOTAL_CAPACITY    = TRACE_CAPACITY + DEBUG_CAPACITY + INFO_CAPACITY
                                              + WARN_CAPACITY + ERROR_CAPACITY + CRITICAL_CAPACITY;

public:
    ImGuiSink(Core::TlsfAllocator* alloc, Core::AllocTag tag)
        : sortedEntries(alloc, tag, TOTAL_CAPACITY)
    {}

    Core::Span<const LogEntry> GetEntries()
    {
        std::lock_guard lock(mutex_);
        sortedEntries.Clear();
        auto append = [&](auto& buf) { buf.ForEach([&](const LogEntry& e) { sortedEntries.PushBack(e); }); };
        append(traceEntries);
        append(debugEntries);
        append(infoEntries);
        append(warnEntries);
        append(errorEntries);
        append(criticalEntries);
        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const LogEntry& a, const LogEntry& b) {
            return a.sequence < b.sequence;
        });
        return {sortedEntries.Data(), sortedEntries.Size()};
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

    /**
     * Registers a name for a Game sub-category
     * @param name
     * @return the existing index if already registered, or -1 if the registry is full.
     */
    int8_t RegisterGameSubCategory(const char* name)
    {
        std::lock_guard lock(mutex_);
        for (size_t i = 0; i < gameSubCategoryNames.Size(); ++i) {
            if (gameSubCategoryNames[i] == name) { return static_cast<int8_t>(i); }
        }
        for (int i = 0; i < (int) LogCategory::Count; ++i) {
            assert(std::string_view(name) != kCategoryNames[i] && "Game sub-category name collides with an engine LogCategory name");
        }
        if (gameSubCategoryNames.IsFull()) { return -1; }
        gameSubCategoryNames.PushBack(Core::InlineString<32>(name));
        return static_cast<int8_t>(gameSubCategoryNames.Size() - 1);
    }

    Core::Span<const Core::InlineString<32>> GetGameSubCategoryNames() const
    {
        return {gameSubCategoryNames.Data(), gameSubCategoryNames.Size()};
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        LogCategory cat = LogCategory::Temp;
        int8_t subCategory = -1;
        for (int i = 0; i < (int) LogCategory::Count; i++) {
            if (msg.logger_name == kCategoryNames[i]) {
                cat = static_cast<LogCategory>(i);
                break;
            }
        }
        if (cat == LogCategory::Temp) {
            for (size_t i = 0; i < gameSubCategoryNames.Size(); ++i) {
                if (gameSubCategoryNames[i].View() == msg.logger_name) {
                    cat = LogCategory::Game;
                    subCategory = static_cast<int8_t>(i);
                    break;
                }
            }
        }

        LogEntry entry{nextSequence++, cat, subCategory, msg.level, Core::InlineString<256>(std::string_view(formatted.data(), formatted.size()))};

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
    Core::FixedVector<LogEntry>                   sortedEntries;
    Core::InlineVector<Core::InlineString<32>, 32> gameSubCategoryNames;
};
} // Engine

#endif //WILL_ENGINE_IMGUI_SINK_H
