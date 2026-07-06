//
// Created by William on 2026-06-13.
//

#include "profile_library.h"

#include <cstdio>
#include <fstream>

#include <json/nlohmann/json.hpp>

#include "platform/paths.h"
#include "platform/file_utils.h"
#include "engine/serialization/config_serialization.h"

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

uint32_t ListLightingProfiles(ProfileName* outNames, uint32_t maxNames)
{
    return ListProfiles("lighting", outNames, maxNames);
}

bool LoadLightingProfile(const char* name, Core::LightingMode& lightingMode, Core::ReSTIRParams& restir, Core::DDGIParams& ddgi, Core::GTAOConfiguration& gtao, StringID& shadingOverride, StringID& lightingOverride, float& iblIntensity)
{
    const nlohmann::json j = ReadProfileJson("lighting", name);
    if (!j.is_object()) {
        return false;
    }
    if (j.contains("lightingMode") && j["lightingMode"].is_number_integer()) {
        uint32_t storedMode = j["lightingMode"].get<uint32_t>();
        if (storedMode == 4u) { storedMode = 1u; }
        lightingMode = static_cast<Core::LightingMode>(storedMode);
    }
    if (j.contains("restir") && j["restir"].is_object()) {
        ConfigSerialization::FromJson(j["restir"], restir);
    }
    if (j.contains("ddgi") && j["ddgi"].is_object()) {
        ConfigSerialization::FromJson(j["ddgi"], ddgi);
    }
    if (j.contains("gtao") && j["gtao"].is_object()) {
        ConfigSerialization::FromJson(j["gtao"], gtao);
    }
    // Migration: iblIntensity used to live inside the restir blob.
    if (j.contains("iblIntensity") && j["iblIntensity"].is_number()) {
        iblIntensity = j["iblIntensity"].get<float>();
    }
    else if (j.contains("restir") && j["restir"].is_object() && j["restir"].contains("iblIntensity") && j["restir"]["iblIntensity"].is_number()) {
        iblIntensity = j["restir"]["iblIntensity"].get<float>();
    }
    shadingOverride = j.contains("shadingShaderOverride") ? StringID(j["shadingShaderOverride"].get<uint64_t>()) : StringID{};
    lightingOverride = j.contains("lightingShaderOverride") ? StringID(j["lightingShaderOverride"].get<uint64_t>()) : StringID{};
    return true;
}

bool SaveLightingProfile(const char* name, Core::LightingMode lightingMode, const Core::ReSTIRParams& restir, const Core::DDGIParams& ddgi, const Core::GTAOConfiguration& gtao, StringID shadingOverride, StringID lightingOverride, float iblIntensity)
{
    nlohmann::json j;
    j["lightingMode"] = static_cast<uint32_t>(lightingMode);
    j["restir"] = ConfigSerialization::ToJson(restir);
    j["ddgi"] = ConfigSerialization::ToJson(ddgi);
    j["gtao"] = ConfigSerialization::ToJson(gtao);
    j["iblIntensity"] = iblIntensity;
    if (shadingOverride) { j["shadingShaderOverride"] = shadingOverride.id; }
    if (lightingOverride) { j["lightingShaderOverride"] = lightingOverride.id; }
    return WriteProfileJson("lighting", name, j);
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
