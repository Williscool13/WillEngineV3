#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "src/engine/will_engine.h"

#include <spdlog/spdlog.h>

#include "platform/crash_handler.h"
#include "platform/paths.h"
#include "utils/logging/logging.h"

#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    Platform::CrashHandler crashHandler(Platform::GetCrashPath() / "crashes");

    Utils::Logger logger(Platform::GetLogPath() / "engine.log");
    SPDLOG_INFO("Engine starting...");

    crashHandler.SetLogPath(logger.GetLogPath());

    Engine::WillEngine we{&crashHandler};
    we.Initialize();
    we.Run();
    we.Cleanup();

    return 0;
}