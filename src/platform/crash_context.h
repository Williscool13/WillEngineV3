//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_CRASH_CONTEXT_H
#define WILLENGINEV3_CRASH_CONTEXT_H
#include <string>
#include <string_view>
#include <json/nlohmann/json.hpp>

namespace Platform
{
class CrashContext
{
public:
    CrashContext();

    void WriteCrashContext(std::string_view crashReason, const std::filesystem::path& folderPath);

private:
    nlohmann::ordered_json context;

#ifdef _WIN32
    void CollectSystemInfoWin32();

    void CollectProcessInfoWin32();
#endif

    static nlohmann::json GetBuildConfiguration();

    static std::string GetTimestamp();
};
} // Platform


#endif //WILLENGINEV3_CRASH_CONTEXT_H
