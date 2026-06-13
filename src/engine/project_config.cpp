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

    if (j.contains("bAutoSave") && j["bAutoSave"].is_boolean()) {
        config.bAutoSave = j["bAutoSave"].get<bool>();
    }

    if (j.contains("lightingMode") && j["lightingMode"].is_number_integer()) {
        config.lightingMode = static_cast<Core::LightingMode>(j["lightingMode"].get<uint32_t>());
    }

    if (j.contains("aaMode") && j["aaMode"].is_number()) {
        config.aaMode = static_cast<Core::AntiAliasingMode>(j["aaMode"].get<int32_t>());
    }
    if (j.contains("restir") && j["restir"].is_object()) {
        ConfigSerialization::FromJson(j["restir"], config.restir);
    }
    if (j.contains("gtao") && j["gtao"].is_object()) {
        ConfigSerialization::FromJson(j["gtao"], config.gtaoConfig);
    }
    if (j.contains("smaa") && j["smaa"].is_object()) {
        ConfigSerialization::FromJson(j["smaa"], config.smaaConfig);
    }
    if (j.contains("taa") && j["taa"].is_object()) {
        ConfigSerialization::FromJson(j["taa"], config.taaConfig);
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
    j["bAutoSave"] = config.bAutoSave;
    j["lightingMode"] = config.lightingMode;
    j["aaMode"] = static_cast<int32_t>(config.aaMode);
    j["restir"] = ConfigSerialization::ToJson(config.restir);
    j["gtao"] = ConfigSerialization::ToJson(config.gtaoConfig);
    j["smaa"] = ConfigSerialization::ToJson(config.smaaConfig);
    j["taa"] = ConfigSerialization::ToJson(config.taaConfig);
    j["postProcess"] = ConfigSerialization::ToJson(config.postProcess);

    file << j.dump(2);
    return file.good();
}
} // Engine
