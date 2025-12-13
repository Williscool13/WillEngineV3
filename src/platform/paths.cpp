//
// Created by William on 2025-12-09.
//

#include "paths.h"
#include <SDL3/SDL_filesystem.h>

namespace Platform
{
std::filesystem::path GetExecutablePath()
{
    const char* basePath = SDL_GetBasePath();
    std::filesystem::path result(basePath);
    return result;
}

std::filesystem::path GetUserDataPath()
{
    char* prefPath = SDL_GetPrefPath("WillEngine", "GameEngine");
    std::filesystem::path result(prefPath);
    SDL_free(prefPath);
    return result;
}

std::filesystem::path GetEngineTempPath()
{
    auto result = std::filesystem::temp_directory_path() / "WillEngine";
    std::filesystem::create_directories(result);
    return result;
}

std::filesystem::path GetShaderPath()
{
    return GetExecutablePath() / "shaders";
}

std::filesystem::path GetLogPath()
{
    // This should be UserDataPath in a packaged build
    return GetExecutablePath() / "logs";
}

std::filesystem::path GetCrashPath()
{
    // This should be UserDataPath in a packaged build
    return GetExecutablePath() / "crash";
}

std::filesystem::path SetWorkingDirectory()
{
    auto exePath = GetExecutablePath();
    std::filesystem::current_path(exePath);
    return exePath;
}
} // Platform