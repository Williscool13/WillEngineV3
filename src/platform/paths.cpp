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

std::filesystem::path GetAssetPath()
{
#ifdef ASSETS_PATH
    return std::filesystem::path(ASSETS_PATH);
#endif
    return GetExecutablePath() / "assets";
}

std::filesystem::path GetLogPath()
{
#ifndef PACKAGED_BUILD
    return GetExecutablePath() / "logs";
#endif
    return GetUserDataPath() / "logs";
}

std::filesystem::path GetCrashPath()
{
#ifndef PACKAGED_BUILD
    return GetExecutablePath() / "crash";
#endif
    return GetUserDataPath() / "crash";
}

std::filesystem::path SetWorkingDirectory()
{
    auto exePath = GetExecutablePath();
    std::filesystem::current_path(exePath);
    return exePath;
}
} // Platform
