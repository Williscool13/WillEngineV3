#include "src/engine/will_engine.h"

#include <spdlog/spdlog.h>

#include "platform/crash_handler.h"
#include "platform/paths.h"
#include "utils/logging/logging.h"

#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    // MEM: std::string bridge until GetCrashPath() returns Core::Path
    std::string crashPathStr = Platform::GetCrashPath().string();
    Platform::CrashHandler crashHandler(crashPathStr.c_str());

    Utils::Logger* _logger = nullptr;
#if LOGGING_ENABLED
    // MEM: spdlog allocates internally; not worth custom allocator since logging is disabled in packaged builds
    // MEM: std::string bridge until GetLogPath() returns Core::Path
    std::string logPathStr = (Platform::GetLogPath() / "engine.log").string();
    Utils::Logger logger(Core::Path(logPathStr.c_str()));
    SPDLOG_INFO("Engine starting...");
    crashHandler.SetLogPath(logger.GetLogPath().c_str());

    _logger = &logger;
#endif

    {
        Engine::WillEngine we{&crashHandler};
        we.Initialize(_logger);
        we.Run();
        we.Cleanup();
    }

#if LOGGING_ENABLED
    logger.ArchiveLogs();
#endif
    return 0;
}
