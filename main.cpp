#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

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

    Engine::WillEngine we{&crashHandler};
    we.Initialize();
    we.Run();
    we.Cleanup();

    return 0;
}