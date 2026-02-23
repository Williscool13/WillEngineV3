#ifndef WILL_ENGINE_ENGINE_LOGGER_H
#define WILL_ENGINE_ENGINE_LOGGER_H

#include <array>
#include <spdlog/spdlog.h>
#include "imgui_sink.h"
#include "log_category.h"
#include "utils/logging/logging.h"

namespace Engine
{
class EngineLogger
{
public:
    void Init(Utils::Logger* baseLogger);

    void Flush() const;

    void Shutdown() const;

    spdlog::logger* GetLogger(LogCategory cat) { return categoryLoggers[(int)cat].get(); }
    ImGuiSink& GetImGuiSink() { return *std::static_pointer_cast<ImGuiSink>(imguiSink); }

    void RegisterLoggersForDLL(LogCategory defaultCategory) const;

private:
    spdlog::sink_ptr imguiSink;
    std::array<std::shared_ptr<spdlog::logger>, static_cast<int>(LogCategory::Count)> categoryLoggers;
};

} // Engine

#endif //WILL_ENGINE_ENGINE_LOGGER_H