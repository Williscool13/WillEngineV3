//
// Created by William on 2026-06-13.
//

#include "profile_library.h"

#include "core/containers/vector.h"
#include "platform/paths.h"
#include "platform/file_utils.h"
#include "engine/engine_api.h"
#include "engine/serialization/config_serialization.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "render/interface/render_interface.h"

namespace Engine::Profiles
{
static constexpr const char* PROFILE_EXTENSION = ".wprofile";

static Core::Path ProfilesDir(const char* subdir)
{
    return Platform::GetConfigPath() / "profiles" / subdir;
}

static Core::Path ProfilePath(const char* subdir, const char* name)
{
    const auto fileName = Core::InlineString<128>::Format("%s%s", name, PROFILE_EXTENSION);
    return ProfilesDir(subdir) / fileName.c_str();
}

static uint32_t ListProfiles(const char* subdir, ProfileName* outNames, uint32_t maxNames)
{
    Core::Path paths[MAX_PROFILES];
    const uint32_t found = Platform::FindFilesByExtension(ProfilesDir(subdir), PROFILE_EXTENSION, paths, MAX_PROFILES);
    const uint32_t count = found < maxNames ? found : maxNames;
    for (uint32_t i = 0; i < count; ++i) {
        outNames[i] = ProfileName(paths[i].Stem());
    }
    return count;
}

static bool WriteProfileText(const char* subdir, const char* name, const Core::Vector<std::byte>& body)
{
    return Platform::WriteFile(ProfilePath(subdir, name), body.Data(), body.Size());
}

static void ApplyLightingBundle(const TextReader& r, LightingProfileBundle& bundle)
{
    bundle.lightingMode = static_cast<Core::LightingMode>(r.UInt("lightingMode", static_cast<uint32_t>(bundle.lightingMode)));
    ConfigSerialization::Deserialize(r.Block("restir"), bundle.restir);
    ConfigSerialization::Deserialize(r.Block("ddgi"), bundle.ddgi);
    ConfigSerialization::Deserialize(r.Block("reflection"), bundle.reflection);
    ConfigSerialization::Deserialize(r.Block("reflectionProbe"), bundle.reflectionProbe);
    ConfigSerialization::Deserialize(r.Block("gtao"), bundle.gtao);
    bundle.iblIntensity = r.Float("iblIntensity", bundle.iblIntensity);
    bundle.indirectIntensity = r.Float("indirectIntensity", bundle.indirectIntensity);
    bundle.shadingOverride = StringID(r.U64("shadingShaderOverride", 0));
    bundle.lightingOverride = StringID(r.U64("lightingShaderOverride", 0));
}

static void BuildLightingBundle(const LightingProfileBundle& bundle, TextWriter& w)
{
    w.Key("lightingMode", static_cast<uint32_t>(bundle.lightingMode));
    w.BeginBlock("restir");
    ConfigSerialization::Serialize(bundle.restir, w);
    w.EndBlock();
    w.BeginBlock("ddgi");
    ConfigSerialization::Serialize(bundle.ddgi, w);
    w.EndBlock();
    w.BeginBlock("reflection");
    ConfigSerialization::Serialize(bundle.reflection, w);
    w.EndBlock();
    w.BeginBlock("reflectionProbe");
    ConfigSerialization::Serialize(bundle.reflectionProbe, w);
    w.EndBlock();
    w.BeginBlock("gtao");
    ConfigSerialization::Serialize(bundle.gtao, w);
    w.EndBlock();
    w.Key("iblIntensity", bundle.iblIntensity);
    w.Key("indirectIntensity", bundle.indirectIntensity);
    if (bundle.shadingOverride) { w.Key("shadingShaderOverride", bundle.shadingOverride.id); }
    if (bundle.lightingOverride) { w.Key("lightingShaderOverride", bundle.lightingOverride.id); }
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
    Platform::ScopedFileMapping map(ProfilePath("lighting", name));
    if (!map.data) {
        return false;
    }
    ApplyLightingBundle(TextReader(map.data, map.size), bundle);
    return true;
}

bool SaveLightingProfile(const char* name, const LightingProfileBundle& bundle, Core::TlsfAllocator* alloc)
{
    Core::Vector<std::byte> body(alloc, Core::AllocTag::EngineState);
    TextWriter w(body);
    BuildLightingBundle(bundle, w);
    return WriteProfileText("lighting", name, body);
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
    Platform::ScopedFileMapping map(ProfilePath("postprocess", name));
    if (!map.data) {
        return false;
    }
    const TextReader r(map.data, map.size);
    ConfigSerialization::Deserialize(r.Block("postProcess"), pp);
    return true;
}

bool SavePostProcessProfile(const char* name, const Core::PostProcessConfiguration& pp, Core::TlsfAllocator* alloc)
{
    Core::Vector<std::byte> body(alloc, Core::AllocTag::EngineState);
    TextWriter w(body);
    w.BeginBlock("postProcess");
    ConfigSerialization::Serialize(pp, w);
    w.EndBlock();
    return WriteProfileText("postprocess", name, body);
}

bool DeletePostProcessProfile(const char* name)
{
    return Platform::DeleteSingleFile(ProfilePath("postprocess", name));
}
}
