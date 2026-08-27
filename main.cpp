#include "src/engine/will_engine.h"

#include <cstring>
#include <cstdlib>

#include <spdlog/spdlog.h>

#include "platform/crash_handler.h"
#include "platform/paths.h"
#include "utils/logging/logging.h"

#include <SDL3/SDL_main.h>

#ifdef TRACY_ENABLE
#include <thread>
#include <tracy/Tracy.hpp>
#endif

/**
 * --scene <name>   load this scene (by .wscene header name) instead of projectConfig.defaultScene
 * --shots <file>   shot-list JSON; activates the capture run
 * --out <dir>      capture PNG output directory
 * --settle <n>     per-shot convergence frames (default probeBake.settleFrames)
 * --exit           quit after the last shot is saved
 * --no-rebar       report no resizable BAR, forcing per frame data through the staged upload path
 */
static Engine::AutomationConfig ParseLaunchArgs(int argc, char* argv[])
{
    Engine::AutomationConfig automation{};
    for (int i = 1; i < argc; i++) {
        const char* value = i + 1 < argc ? argv[i + 1] : "";
        if (strcmp(argv[i], "--scene") == 0) {
            automation.sceneOverride = Core::InlineString<256>(value);
            i++;
        }
        else if (strcmp(argv[i], "--shots") == 0) {
            automation.shotsPath = Core::InlineString<512>(value);
            i++;
        }
        else if (strcmp(argv[i], "--out") == 0) {
            automation.outputDir = Core::InlineString<512>(value);
            i++;
        }
        else if (strcmp(argv[i], "--settle") == 0) {
            automation.settleFrames = atoi(value);
            i++;
        }
        else if (strcmp(argv[i], "--exit") == 0) {
            automation.bExitWhenDone = true;
        }
        else if (strcmp(argv[i], "--no-rebar") == 0) {
            automation.bForceNoREBAR = true;
        }
    }
    return automation;
}

int main(int argc, char* argv[])
{
    Platform::CrashHandler crashHandler(Platform::GetCrashPath().c_str());

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
        we.Initialize(_logger, ParseLaunchArgs(argc, argv));
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
