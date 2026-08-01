//
// Created by William on 2026-06-13.
//

#include "profile_library.h"

#include <cstdio>
#include <fstream>

#include <json/nlohmann/json.hpp>

#include "platform/paths.h"
#include "platform/file_utils.h"
#include "engine/engine_api.h"
#include "engine/serialization/config_serialization.h"
#include "render/interface/render_interface.h"

namespace Engine::Profiles
{
static constexpr const char* kExtension = ".wprofile";

static Core::Path ProfilesDir(const char* subdir)
{
    return Platform::GetConfigPath() / "profiles" / subdir;
}

static Core::Path ProfilePath(const char* subdir, const char* name)
{
    char fileName[128];
    std::snprintf(fileName, sizeof(fileName), "%s%s", name, kExtension);
    return ProfilesDir(subdir) / fileName;
}

static uint32_t ListProfiles(const char* subdir, ProfileName* outNames, uint32_t maxNames)
{
    Core::Path paths[MAX_PROFILES];
    const uint32_t found = Platform::FindFilesByExtension(ProfilesDir(subdir), kExtension, paths, MAX_PROFILES);
    const uint32_t count = found < maxNames ? found : maxNames;
    for (uint32_t i = 0; i < count; ++i) {
        outNames[i] = ProfileName(paths[i].Stem());
    }
    return count;
}

static nlohmann::json ReadProfileJson(const char* subdir, const char* name)
{
    std::ifstream file(ProfilePath(subdir, name).c_str());
    if (!file.is_open()) {
        return {};
    }
    return nlohmann::json::parse(file, nullptr, false);
}

static bool WriteProfileJson(const char* subdir, const char* name, const nlohmann::json& j)
{
    const std::string dump = j.dump(2);
    return Platform::WriteFile(ProfilePath(subdir, name), std::string_view(dump));
}

static bool ApplyLightingBundle(const nlohmann::json& j, LightingProfileBundle& bundle)
{
    if (!j.is_object()) {
        return false;
    }
    if (j.contains("lightingMode") && j["lightingMode"].is_number_integer()) {
        uint32_t storedMode = j["lightingMode"].get<uint32_t>();
        bundle.lightingMode = static_cast<Core::LightingMode>(storedMode);
    }
    if (j.contains("restir") && j["restir"].is_object()) {
        ConfigSerialization::FromJson(j["restir"], bundle.restir);
    }
    if (j.contains("ddgi") && j["ddgi"].is_object()) {
        ConfigSerialization::FromJson(j["ddgi"], bundle.ddgi);
    }
    if (j.contains("reflection") && j["reflection"].is_object()) {
        ConfigSerialization::FromJson(j["reflection"], bundle.reflection);
    }
    if (j.contains("reflectionProbe") && j["reflectionProbe"].is_object()) {
        ConfigSerialization::FromJson(j["reflectionProbe"], bundle.reflectionProbe);
    }
    if (j.contains("gtao") && j["gtao"].is_object()) {
        ConfigSerialization::FromJson(j["gtao"], bundle.gtao);
    }
    // Migration: iblIntensity used to live inside the restir blob.
    if (j.contains("iblIntensity") && j["iblIntensity"].is_number()) {
        bundle.iblIntensity = j["iblIntensity"].get<float>();
    }
    else if (j.contains("restir") && j["restir"].is_object() && j["restir"].contains("iblIntensity") && j["restir"]["iblIntensity"].is_number()) {
        bundle.iblIntensity = j["restir"]["iblIntensity"].get<float>();
    }
    if (j.contains("indirectIntensity") && j["indirectIntensity"].is_number()) {
        bundle.indirectIntensity = j["indirectIntensity"].get<float>();
    }
    bundle.shadingOverride = j.contains("shadingShaderOverride") ? StringID(j["shadingShaderOverride"].get<uint64_t>()) : StringID{};
    bundle.lightingOverride = j.contains("lightingShaderOverride") ? StringID(j["lightingShaderOverride"].get<uint64_t>()) : StringID{};
    return true;
}

static nlohmann::json BuildLightingBundle(const LightingProfileBundle& bundle)
{
    nlohmann::json j;
    j["lightingMode"] = static_cast<uint32_t>(bundle.lightingMode);
    j["restir"] = ConfigSerialization::ToJson(bundle.restir);
    j["ddgi"] = ConfigSerialization::ToJson(bundle.ddgi);
    j["reflection"] = ConfigSerialization::ToJson(bundle.reflection);
    j["reflectionProbe"] = ConfigSerialization::ToJson(bundle.reflectionProbe);
    j["gtao"] = ConfigSerialization::ToJson(bundle.gtao);
    j["iblIntensity"] = bundle.iblIntensity;
    j["indirectIntensity"] = bundle.indirectIntensity;
    if (bundle.shadingOverride) { j["shadingShaderOverride"] = bundle.shadingOverride.id; }
    if (bundle.lightingOverride) { j["lightingShaderOverride"] = bundle.lightingOverride.id; }
    return j;
}

uint32_t ListLightingProfiles(ProfileName* outNames, uint32_t maxNames)
{
    return ListProfiles("lighting", outNames, maxNames);
}

LightingProfileBundle CaptureLightingProfile(const EngineState& state)
{
    LightingProfileBundle bundle;
    bundle.lightingMode = state.lighting.lightingMode;
    bundle.restir = state.debug.restir;
    bundle.ddgi = state.lighting.ddgi;
    bundle.reflection = state.lighting.reflection;
    bundle.reflectionProbe = state.lighting.reflectionProbe;
    bundle.gtao = state.lighting.gtaoConfig;
    bundle.shadingOverride = state.debug.shadingShaderOverride;
    bundle.lightingOverride = state.debug.lightingShaderOverride;
    bundle.iblIntensity = state.lighting.iblIntensity;
    bundle.indirectIntensity = state.lighting.indirectIntensity;
    return bundle;
}

void ApplyLightingProfile(EngineState& state, const LightingProfileBundle& bundle)
{
    state.lighting.lightingMode = bundle.lightingMode;
    state.debug.restir = bundle.restir;
    state.lighting.ddgi = bundle.ddgi;
    state.lighting.reflection = bundle.reflection;
    state.lighting.reflectionProbe = bundle.reflectionProbe;
    state.lighting.gtaoConfig = bundle.gtao;
    state.debug.shadingShaderOverride = bundle.shadingOverride;
    state.debug.lightingShaderOverride = bundle.lightingOverride;
    state.lighting.iblIntensity = bundle.iblIntensity;
    state.lighting.indirectIntensity = bundle.indirectIntensity;
}

bool LoadLightingProfile(const char* name, LightingProfileBundle& bundle)
{
    return ApplyLightingBundle(ReadProfileJson("lighting", name), bundle);
}

bool SaveLightingProfile(const char* name, const LightingProfileBundle& bundle)
{
    return WriteProfileJson("lighting", name, BuildLightingBundle(bundle));
}

bool DeleteLightingProfile(const char* name)
{
    return Platform::DeleteSingleFile(ProfilePath("lighting", name));
}

uint32_t ListPostProcessProfiles(ProfileName* outNames, uint32_t maxNames)
{
    return ListProfiles("postprocess", outNames, maxNames);
}

bool LoadPostProcessProfile(const char* name, Core::PostProcessConfiguration& pp)
{
    const nlohmann::json j = ReadProfileJson("postprocess", name);
    if (!j.is_object()) {
        return false;
    }
    if (j.contains("postProcess") && j["postProcess"].is_object()) {
        ConfigSerialization::FromJson(j["postProcess"], pp);
    }
    return true;
}

bool SavePostProcessProfile(const char* name, const Core::PostProcessConfiguration& pp)
{
    nlohmann::json j;
    j["postProcess"] = ConfigSerialization::ToJson(pp);
    return WriteProfileJson("postprocess", name, j);
}

bool DeletePostProcessProfile(const char* name)
{
    return Platform::DeleteSingleFile(ProfilePath("postprocess", name));
}
}
