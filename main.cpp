#include "src/engine/will_engine.h"

#include <spdlog/spdlog.h>

#include "platform/crash_handler.h"
#include "platform/paths.h"
#include "utils/logging/logging.h"

#include <SDL3/SDL_main.h>

#ifdef TRACY_ENABLE
#include <thread>
#include <tracy/Tracy.hpp>
#endif

int main(int argc, char* argv[])
{
    Platform::CrashHandler crashHandler(Platform::GetCrashPath().c_str());

    static Engine::Args::LaunchArgs launchArgs;
    Engine::Args::Parse(launchArgs, argc, argv);

    Utils::Logger* _logger = nullptr;
#if LOGGING_ENABLED
    // MEM: spdlog allocates internally; not worth custom allocator since logging is disabled in packaged builds
    Utils::Logger logger(Core::Path((Platform::GetLogPath() / "engine.log").c_str()));
    SPDLOG_INFO("Engine starting...");
    crashHandler.SetLogPath(logger.GetLogPath().c_str());

    _logger = &logger;
#endif

    {
        Engine::WillEngine we{&crashHandler};
        we.Initialize(_logger, &launchArgs);
        we.Run();
        we.Cleanup();
    }

#if LOGGING_ENABLED
    logger.ArchiveLogs();
#endif

#ifdef TRACY_ENABLE
    tracy::GetProfiler().RequestShutdown();
    while (!tracy::GetProfiler().HasShutdownFinished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
    return 0;
}
