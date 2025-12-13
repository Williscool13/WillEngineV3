//
// Created by William on 2025-12-09.
//

#ifndef WILL_ENGINE_PLATFORM_H
#define WILL_ENGINE_PLATFORM_H

#include <filesystem>

namespace Platform
{
inline constexpr int32_t MAX_PATH_LENGTH = 1024;

std::filesystem::path GetExecutablePath();

std::filesystem::path GetUserDataPath();

std::filesystem::path GetEngineTempPath();

std::filesystem::path GetTempPath();

std::filesystem::path GetLogPath();

std::filesystem::path GetCrashPath();

std::filesystem::path GetShaderPath();
}

#endif //WILL_ENGINE_PLATFORM_H
