#ifndef WILL_ENGINE_ENGINE_LOGGER_H
#define WILL_ENGINE_ENGINE_LOGGER_H

#include <spdlog/spdlog.h>
#include "imgui_sink.h"
#include "log_category.h"
#include "utils/logging/logging.h"
#include "core/containers/array.h"
#include "core/memory/memory_manager.h"


namespace Engine
{
class EngineLogger
{
public:
    EngineLogger() = default;
    explicit EngineLogger(Core::MemoryManager& memoryManager);
    ~EngineLogger() = default;
    void Init(Utils::Logger* baseLogger);

    void Flush() const;

    void Shutdown() const;

    spdlog::logger* GetLogger(LogCategory cat) { return categoryLoggers[static_cast<int>(cat)].get(); }
    ImGuiSink* GetImGuiSink() const { return imguiSink; }

    void RegisterLoggersForDLL(LogCategory defaultCategory) const;

private:
    ImGuiSink* imguiSink;
    Core::Array<std::shared_ptr<spdlog::logger>, static_cast<size_t>(LogCategory::Count)> categoryLoggers;
};

} // Engine

#endif //WILL_ENGINE_ENGINE_LOGGER_H