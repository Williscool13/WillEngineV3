//
// Created by William on 2026-04-22.
//

#include "project_config.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "platform/paths.h"
#include "engine/serialization/config_serialization.h"

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

    if (j.contains("bLimitFps") && j["bLimitFps"].is_boolean()) {
        config.bLimitFps = j["bLimitFps"].get<bool>();
    }
    if (j.contains("frameLimitTarget") && j["frameLimitTarget"].is_number_integer()) {
        config.frameLimitTarget = j["frameLimitTarget"].get<int32_t>();
    }

    if (j.contains("bAutoSaveProjectConfig") && j["bAutoSaveProjectConfig"].is_boolean()) {
        config.bAutoSaveProjectConfig = j["bAutoSaveProjectConfig"].get<bool>();
    }
    if (j.contains("bAutoSaveLighting") && j["bAutoSaveLighting"].is_boolean()) {
        config.bAutoSaveLighting = j["bAutoSaveLighting"].get<bool>();
    }
    if (j.contains("bAutoSavePostProcess") && j["bAutoSavePostProcess"].is_boolean()) {
        config.bAutoSavePostProcess = j["bAutoSavePostProcess"].get<bool>();
    }
    if (j.contains("activeLightingProfile") && j["activeLightingProfile"].is_string()) {
        config.activeLightingProfile = Core::InlineString<64>(j["activeLightingProfile"].get<std::string_view>());
    }
    if (j.contains("activePostProcessProfile") && j["activePostProcessProfile"].is_string()) {
        config.activePostProcessProfile = Core::InlineString<64>(j["activePostProcessProfile"].get<std::string_view>());
    }

    if (j.contains("lightingMode") && j["lightingMode"].is_number_integer()) {
        config.lightingMode = static_cast<Core::LightingMode>(j["lightingMode"].get<uint32_t>());
    }

    if (j.contains("restir") && j["restir"].is_object()) {
        ConfigSerialization::FromJson(j["restir"], config.restir);
    }
    if (j.contains("gtao") && j["gtao"].is_object()) {
        ConfigSerialization::FromJson(j["gtao"], config.gtaoConfig);
    }
    if (j.contains("aa") && j["aa"].is_object()) {
        ConfigSerialization::FromJson(j["aa"], config.aaConfig);
    }
    if (j.contains("postProcess") && j["postProcess"].is_object()) {
        ConfigSerialization::FromJson(j["postProcess"], config.postProcess);
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
    j["bLimitFps"] = config.bLimitFps;
    j["frameLimitTarget"] = config.frameLimitTarget;
    j["bAutoSaveProjectConfig"] = config.bAutoSaveProjectConfig;
    j["bAutoSaveLighting"] = config.bAutoSaveLighting;
    j["bAutoSavePostProcess"] = config.bAutoSavePostProcess;
    j["activeLightingProfile"] = std::string_view(config.activeLightingProfile.c_str(), config.activeLightingProfile.Size());
    j["activePostProcessProfile"] = std::string_view(config.activePostProcessProfile.c_str(), config.activePostProcessProfile.Size());
    j["lightingMode"] = config.lightingMode;
    j["restir"] = ConfigSerialization::ToJson(config.restir);
    j["gtao"] = ConfigSerialization::ToJson(config.gtaoConfig);
    j["aa"] = ConfigSerialization::ToJson(config.aaConfig);
    j["postProcess"] = ConfigSerialization::ToJson(config.postProcess);

    file << j.dump(2);
    return file.good();
}
} // Engine
