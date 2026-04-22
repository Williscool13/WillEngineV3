//
// Created by William on 2026-04-22.
//

#include "project_config.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "platform/paths.h"

namespace Engine
{
static Core::Path GetProjectConfigPath()
{
    return Platform::GetConfigPath() / "project.wconfig";
}

ProjectConfig ReadProjectConfig()
{
    ProjectConfig config{};

    const Core::Path path = GetProjectConfigPath();
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        return config;
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        return config;
    }

    if (j.contains("defaultScene") && j["defaultScene"].is_string()) {
        config.defaultScene = Core::InlineString<256>(j["defaultScene"].get<std::string_view>());
    }

    return config;
}

bool WriteProjectConfig(const ProjectConfig& config)
{
    const Core::Path path = GetProjectConfigPath();
    std::ofstream file(path.c_str());
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json j;
    j["defaultScene"] = std::string_view(config.defaultScene.c_str(), config.defaultScene.Size());

    file << j.dump(2);
    return file.good();
}
} // Engine
