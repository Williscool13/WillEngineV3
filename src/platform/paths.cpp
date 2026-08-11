//
// Created by William on 2025-12-09.
//

#include "paths.h"

#include <cstdlib>

#include <windows.h>

#include "file_utils.h"

namespace Platform
{
const Core::Path& GetExecutablePath()
{
    static const Core::Path path = []() {
        char buf[1024];
        const DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
        DWORD end = len;
        while (end > 0 && buf[end - 1] != '\\' && buf[end - 1] != '/') { end--; }
        buf[end] = '\0';
        return Core::Path(buf);
    }();
    return path;
}

const Core::Path& GetUserDataPath()
{
    static const Core::Path path = []() {
        const char* appData = getenv("APPDATA");
        Core::Path p = Core::Path(appData != nullptr ? appData : ".") / "WillEngine" / "GameEngine";
        CreateDirectories(p.c_str());
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
#ifdef CONFIG_PATH
        auto p = Core::Path(CONFIG_PATH);
#else
        Core::Path p = GetExecutablePath() / "config";
#endif
        CreateDirectories(p.c_str());
        return p;
    }();
    return path;
}
} // Platform
