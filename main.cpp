#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "src/engine/will_engine.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "platform/crash_handler.h"
#include "platform/paths.h"
#include "utils/logging/logging.h"

int main()
{
    Platform::CrashHandler crashHandler(Platform::GetCrashPath() / "crashes");

    Utils::Logger logger(Platform::GetLogPath() / "engine.log");
    spdlog::info("Engine starting...");

    crashHandler.SetLogPath(logger.GetLogPath());

    Engine::WillEngine we{&crashHandler};
    we.Initialize();
    we.Run();
    we.Cleanup();

    return 0;
}