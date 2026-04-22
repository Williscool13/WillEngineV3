//
// Created by William on 2025-12-09.
//

#ifndef WILL_ENGINE_PLATFORM_H
#define WILL_ENGINE_PLATFORM_H

#include "core/containers/inline_path.h"

namespace Platform
{
inline constexpr int32_t MAX_PATH_LENGTH = 1024;

const Core::Path& GetExecutablePath();

const Core::Path& GetUserDataPath();

const Core::Path& GetEngineTempPath();

const Core::Path& GetLogPath();

const Core::Path& GetCrashPath();

const Core::Path& GetShaderPath();

const Core::Path& GetAssetPath();

const Core::Path& GetCachePath();

const Core::Path& GetConfigPath();
}

#endif //WILL_ENGINE_PLATFORM_H
