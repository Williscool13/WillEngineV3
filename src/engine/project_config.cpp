//
// Created by William on 2026-04-22.
//

#include "project_config.h"

#include "core/containers/vector.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "engine/serialization/config_serialization.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

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
    Platform::ScopedFileMapping map(path);
    if (!map.data) {
        return config;
    }

    const TextReader r(map.data, map.size);

    r.Str("defaultScene", config.defaultScene);

    config.bLimitFps = r.Bool("bLimitFps", config.bLimitFps);
    config.frameLimitTarget = r.Int("frameLimitTarget", config.frameLimitTarget);

    config.bAutoSaveProjectConfig = r.Bool("bAutoSaveProjectConfig", config.bAutoSaveProjectConfig);
    config.bAutoSaveLighting = r.Bool("bAutoSaveLighting", config.bAutoSaveLighting);
    config.bAutoSavePostProcess = r.Bool("bAutoSavePostProcess", config.bAutoSavePostProcess);
    r.Str("activeLightingProfile", config.activeLightingProfile);
    r.Str("activePostProcessProfile", config.activePostProcessProfile);
    r.Str("activeInputProfile", config.activeInputProfile);

    ConfigSerialization::Deserialize(r.Block("aa"), config.aaConfig);

    config.resolutionScale = r.Float("resolutionScale", config.resolutionScale);
    config.reflectionProbeLineWidth = r.Float("reflectionProbeLineWidth", config.reflectionProbeLineWidth);

    config.gameCameraFovDegrees = r.Float("gameCameraFovDegrees", config.gameCameraFovDegrees);
    config.gameCameraNearPlane = r.Float("gameCameraNearPlane", config.gameCameraNearPlane);
    config.editorCameraFovDegrees = r.Float("editorCameraFovDegrees", config.editorCameraFovDegrees);
    config.editorCameraNearPlane = r.Float("editorCameraNearPlane", config.editorCameraNearPlane);
    config.gameCameraLockAspect = r.Bool("gameCameraLockAspect", config.gameCameraLockAspect);
    config.gameCameraAspect = r.Vec2("gameCameraAspect", config.gameCameraAspect);

    const TextReader b = r.Block("probeBake");
    config.probeBake.settleFrames = b.Int("settleFrames", config.probeBake.settleFrames);
    config.probeBake.captureSize = b.Int("captureSize", config.probeBake.captureSize);
    config.probeBake.bAutoConverge = b.Bool("bAutoConverge", config.probeBake.bAutoConverge);
    config.probeBake.bAutoFreeze = b.Bool("bAutoFreeze", config.probeBake.bAutoFreeze);
    config.probeBake.bGroundTruth = b.Bool("bGroundTruth", config.probeBake.bGroundTruth);
    config.probeBake.groundTruthSpp = b.Int("groundTruthSpp", config.probeBake.groundTruthSpp);

    size_t presetIndex = 0;
    r.ForEachRecord("cameraPresets", [&](const TextReader& e) {
        if (presetIndex >= MAX_CAMERA_PRESETS) { return; }
        CameraPreset& p = config.cameraPresets[presetIndex++];
        p.bSet = e.Bool("set", p.bSet);
        p.translation = e.Vec3("t", p.translation);
        p.rotation = e.Quat("r", p.rotation);
    });

    return config;
}

bool WriteProjectConfig(const ProjectConfig& config, Core::TlsfAllocator* alloc)
{
    Core::Vector<std::byte> body(alloc, Core::AllocTag::EngineState);
    TextWriter w(body);

    w.KeyStr("defaultScene", config.defaultScene.View());
    w.Key("bLimitFps", config.bLimitFps);
    w.Key("frameLimitTarget", config.frameLimitTarget);
    w.Key("bAutoSaveProjectConfig", config.bAutoSaveProjectConfig);
    w.Key("bAutoSaveLighting", config.bAutoSaveLighting);
    w.Key("bAutoSavePostProcess", config.bAutoSavePostProcess);
    w.KeyStr("activeLightingProfile", config.activeLightingProfile.View());
    w.KeyStr("activePostProcessProfile", config.activePostProcessProfile.View());
    w.KeyStr("activeInputProfile", config.activeInputProfile.View());
    w.BeginBlock("aa");
    ConfigSerialization::Serialize(config.aaConfig, w);
    w.EndBlock();
    w.Key("resolutionScale", config.resolutionScale);
    w.Key("reflectionProbeLineWidth", config.reflectionProbeLineWidth);
    w.Key("gameCameraFovDegrees", config.gameCameraFovDegrees);
    w.Key("gameCameraNearPlane", config.gameCameraNearPlane);
    w.Key("editorCameraFovDegrees", config.editorCameraFovDegrees);
    w.Key("editorCameraNearPlane", config.editorCameraNearPlane);
    w.Key("gameCameraLockAspect", config.gameCameraLockAspect);
    w.Key("gameCameraAspect", config.gameCameraAspect);

    w.BeginBlock("probeBake");
    w.Key("settleFrames", config.probeBake.settleFrames);
    w.Key("captureSize", config.probeBake.captureSize);
    w.Key("bAutoConverge", config.probeBake.bAutoConverge);
    w.Key("bAutoFreeze", config.probeBake.bAutoFreeze);
    w.Key("bGroundTruth", config.probeBake.bGroundTruth);
    w.Key("groundTruthSpp", config.probeBake.groundTruthSpp);
    w.EndBlock();

    w.Count("cameraPresets", MAX_CAMERA_PRESETS);
    for (const CameraPreset& p : config.cameraPresets) {
        w.BeginBlock("p");
        w.Key("set", p.bSet);
        w.Key("t", p.translation);
        w.Key("r", p.rotation);
        w.EndBlock();
    }

    return Platform::WriteFile(GetProjectConfigPath(), body.Data(), body.Size());
}
} // Engine
