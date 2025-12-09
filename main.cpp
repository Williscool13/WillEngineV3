#include "core/will_engine.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "platform/crash_handler.h"
#include "utils/logging/logging.h"

int main()
{
    Utils::Logger logger("logs/engine.log");
    spdlog::info("Engine starting...");

    Platform::CrashHandler crashHandler("crashes/");
    crashHandler.SetLogPath(logger.GetLogPath());

    int* ptr = nullptr;
    int value = *ptr;
    fmt::println("{}", value);

    // crashHandler.TriggerManualDump("Testing manual crash dump");

    Engine::WillEngine we{};
    we.Initialize();
    we.Run();
    we.Cleanup();

    return 0;
}