//
// Created by William on 2025-12-09.
//

#include "paths.h"

#include <SDL3/SDL_filesystem.h>

#include "file_utils.h"

namespace Platform
{
const Core::Path& GetExecutablePath()
{
    static const Core::Path path(SDL_GetBasePath());
    return path;
}

const Core::Path& GetUserDataPath()
{
    static const Core::Path path = []() {
        char* prefPath = SDL_GetPrefPath("WillEngine", "GameEngine");
        Core::Path p(prefPath);
        SDL_free(prefPath);
        return p;
    }();
    return path;
}

const Core::Path& GetEngineTempPath()
{
    static const Core::Path path = []() {
        Core::Path p = GetExecutablePath() / "temp";
        CreateDirectories(p.c_str());
        return p;
    }();
    return path;
}

const Core::Path& GetShaderPath()
{
    static const Core::Path path = GetExecutablePath() / "shaders";
    return path;
}

const Core::Path& GetAssetPath()
{
#ifdef ASSETS_PATH
    static const Core::Path path = Core::Path(ASSETS_PATH);
    return path;
#else
    static const Core::Path path = GetExecutablePath() / "assets";
    return path;
#endif
}

const Core::Path& GetLogPath()
{
#ifndef PACKAGED_BUILD
    static const Core::Path path = GetExecutablePath() / "logs";
#else
    static const Core::Path path = GetUserDataPath() / "logs";
#endif
    return path;
}

const Core::Path& GetCrashPath()
{
#ifndef PACKAGED_BUILD
    static const Core::Path path = GetExecutablePath() / "crash";
#else
    static const Core::Path path = GetUserDataPath() / "crashes";
#endif
    return path;
}

const Core::Path& GetCachePath()
{
    static const Core::Path path = []() {
#ifndef PACKAGED_BUILD
        Core::Path p = GetExecutablePath() / "cache";
#else
        Core::Path p = GetUserDataPath() / "cache";
#endif
        CreateDirectories(p.c_str());
        return p;
    }();
    return path;
}

const Core::Path& GetConfigPath()
{
    static const Core::Path path = []() {
#ifdef ASSETS_PATH
        Core::Path p = Core::Path(ASSETS_PATH) / "config";
#else
        Core::Path p = GetExecutablePath() / "config";
#endif
        CreateDirectories(p.c_str());
        return p;
    }();
    return path;
}
} // Platform
