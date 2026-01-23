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
#else
    return GetExecutablePath() / "assets";
#endif
}

std::filesystem::path GetLogPath()
{
#ifndef PACKAGED_BUILD
    return GetExecutablePath() / "logs";
#else
    return GetUserDataPath() / "logs";
#endif
}

std::filesystem::path GetCrashPath()
{
#ifndef PACKAGED_BUILD
    return GetExecutablePath() / "crash";
#else
    return GetUserDataPath() / "crash";
#endif
}

std::filesystem::path SetWorkingDirectory()
{
    auto exePath = GetExecutablePath();
    std::filesystem::current_path(exePath);
    return exePath;
}

std::filesystem::path GetCachePath()
{
#ifndef PACKAGED_BUILD
    auto result = GetExecutablePath() / "cache";
    std::filesystem::create_directories(result);
    return result;
#else
    auto result = GetUserDataPath() / "cache";
    std::filesystem::create_directories(result);
    return result;
#endif
}
} // Platform
