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

    if (j.contains("aa") && j["aa"].is_object()) {
        ConfigSerialization::FromJson(j["aa"], config.aaConfig);
    }

    if (j.contains("gameCameraFovDegrees") && j["gameCameraFovDegrees"].is_number()) {
        config.gameCameraFovDegrees = j["gameCameraFovDegrees"].get<float>();
    }
    if (j.contains("gameCameraNearPlane") && j["gameCameraNearPlane"].is_number()) {
        config.gameCameraNearPlane = j["gameCameraNearPlane"].get<float>();
    }
    if (j.contains("editorCameraFovDegrees") && j["editorCameraFovDegrees"].is_number()) {
        config.editorCameraFovDegrees = j["editorCameraFovDegrees"].get<float>();
    }
    if (j.contains("editorCameraNearPlane") && j["editorCameraNearPlane"].is_number()) {
        config.editorCameraNearPlane = j["editorCameraNearPlane"].get<float>();
    }
    if (j.contains("gameCameraLockAspect") && j["gameCameraLockAspect"].is_boolean()) {
        config.gameCameraLockAspect = j["gameCameraLockAspect"].get<bool>();
    }
    if (j.contains("gameCameraAspect") && j["gameCameraAspect"].is_array() && j["gameCameraAspect"].size() == 2) {
        config.gameCameraAspect = Vec2(j["gameCameraAspect"][0].get<float>(), j["gameCameraAspect"][1].get<float>());
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
    j["aa"] = ConfigSerialization::ToJson(config.aaConfig);
    j["gameCameraFovDegrees"] = config.gameCameraFovDegrees;
    j["gameCameraNearPlane"] = config.gameCameraNearPlane;
    j["editorCameraFovDegrees"] = config.editorCameraFovDegrees;
    j["editorCameraNearPlane"] = config.editorCameraNearPlane;
    j["gameCameraLockAspect"] = config.gameCameraLockAspect;
    j["gameCameraAspect"] = {config.gameCameraAspect.x, config.gameCameraAspect.y};

    file << j.dump(2);
    return file.good();
}
} // Engine
