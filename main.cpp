#include "src/engine/will_engine.h"

#include <spdlog/spdlog.h>

#include "platform/crash_handler.h"
#include "platform/paths.h"
#include "utils/logging/logging.h"

#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    Platform::CrashHandler crashHandler(Platform::GetCrashPath());

#ifndef LOGGING_DISABLED
    Utils::Logger logger(Platform::GetLogPath() / "engine.log");
    SPDLOG_INFO("Engine starting...");
    crashHandler.SetLogPath(logger.GetLogPath());
#endif

    Engine::WillEngine we{&crashHandler};
    we.Initialize();
    we.Run();
    we.Cleanup();

#ifndef LOGGING_DISABLED
    logger.ArchiveLogs();
#endif
    return 0;
}
