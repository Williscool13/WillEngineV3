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
    if (j.contains("activeInputProfile") && j["activeInputProfile"].is_string()) {
        config.activeInputProfile = Core::InlineString<64>(j["activeInputProfile"].get<std::string_view>());
    }

    if (j.contains("aa") && j["aa"].is_object()) {
        ConfigSerialization::FromJson(j["aa"], config.aaConfig);
    }

    if (j.contains("resolutionScale") && j["resolutionScale"].is_number()) {
        config.resolutionScale = j["resolutionScale"].get<float>();
    }

    if (j.contains("reflectionProbeLineWidth") && j["reflectionProbeLineWidth"].is_number()) {
        config.reflectionProbeLineWidth = j["reflectionProbeLineWidth"].get<float>();
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

    if (j.contains("probeBake") && j["probeBake"].is_object()) {
        const nlohmann::json& b = j["probeBake"];
        if (b.contains("settleFrames") && b["settleFrames"].is_number_integer()) {
            config.probeBake.settleFrames = b["settleFrames"].get<int32_t>();
        }
        if (b.contains("captureSize") && b["captureSize"].is_number_integer()) {
            config.probeBake.captureSize = b["captureSize"].get<int32_t>();
        }
        if (b.contains("bAutoConverge") && b["bAutoConverge"].is_boolean()) {
            config.probeBake.bAutoConverge = b["bAutoConverge"].get<bool>();
        }
        if (b.contains("bAutoFreeze") && b["bAutoFreeze"].is_boolean()) {
            config.probeBake.bAutoFreeze = b["bAutoFreeze"].get<bool>();
        }
    }

    if (j.contains("cameraPresets") && j["cameraPresets"].is_array()) {
        const nlohmann::json& presets = j["cameraPresets"];
        const size_t count = presets.size() < MAX_CAMERA_PRESETS ? presets.size() : MAX_CAMERA_PRESETS;
        for (size_t i = 0; i < count; ++i) {
            const nlohmann::json& e = presets[i];
            CameraPreset& p = config.cameraPresets[i];
            if (e.contains("set") && e["set"].is_boolean()) {
                p.bSet = e["set"].get<bool>();
            }
            if (e.contains("t") && e["t"].is_array() && e["t"].size() == 3) {
                p.translation = Vec3(e["t"][0].get<float>(), e["t"][1].get<float>(), e["t"][2].get<float>());
            }
            if (e.contains("r") && e["r"].is_array() && e["r"].size() == 4) {
                p.rotation = Quat(e["r"][3].get<float>(), e["r"][0].get<float>(), e["r"][1].get<float>(), e["r"][2].get<float>());
            }
        }
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
    j["activeInputProfile"] = std::string_view(config.activeInputProfile.c_str(), config.activeInputProfile.Size());
    j["aa"] = ConfigSerialization::ToJson(config.aaConfig);
    j["resolutionScale"] = config.resolutionScale;
    j["reflectionProbeLineWidth"] = config.reflectionProbeLineWidth;
    j["gameCameraFovDegrees"] = config.gameCameraFovDegrees;
    j["gameCameraNearPlane"] = config.gameCameraNearPlane;
    j["editorCameraFovDegrees"] = config.editorCameraFovDegrees;
    j["editorCameraNearPlane"] = config.editorCameraNearPlane;
    j["gameCameraLockAspect"] = config.gameCameraLockAspect;
    j["gameCameraAspect"] = {config.gameCameraAspect.x, config.gameCameraAspect.y};

    j["probeBake"] = {
        {"settleFrames", config.probeBake.settleFrames},
        {"captureSize", config.probeBake.captureSize},
        {"bAutoConverge", config.probeBake.bAutoConverge},
        {"bAutoFreeze", config.probeBake.bAutoFreeze},
    };

    nlohmann::json presets = nlohmann::json::array();
    for (const CameraPreset& p : config.cameraPresets) {
        nlohmann::json e;
        e["set"] = p.bSet;
        e["t"] = {p.translation.x, p.translation.y, p.translation.z};
        e["r"] = {p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w};
        presets.push_back(e);
    }
    j["cameraPresets"] = presets;

    file << j.dump(2);
    return file.good();
}
} // Engine
