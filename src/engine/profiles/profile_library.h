//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_PROFILE_LIBRARY_H
#define WILL_ENGINE_PROFILE_LIBRARY_H

#include <cstdint>

#include "core/containers/inline_string.h"
#include "core/string_id.h"
#include "render/interface/render_interface.h"

namespace Core
{
struct PostProcessConfiguration;
class TlsfAllocator;
}

namespace Engine
{
struct EngineState;
}

namespace Engine::Profiles
{
inline constexpr uint32_t MAX_PROFILES = 64;

using ProfileName = Core::InlineString<64>;

/** Every live config a lighting profile covers; the members are spread across EngineState::lighting and ::debug. */
struct LightingProfileBundle
{
    Core::LightingMode lightingMode{};
    Core::ReSTIRParams restir{};
    Core::DDGIParams ddgi{};
    Core::ReflectionConfiguration reflection{};
    Core::ReflectionProbeConfiguration reflectionProbe{};
    Core::GTAOConfiguration gtao{};
    StringID shadingOverride{};
    StringID lightingOverride{};
    float iblIntensity{1.0f};
    float indirectIntensity{1.0f};
};

/**
 * Named config presets stored one-per-file under <config>/profiles/{lighting,postprocess}/*.wprofile.
 * A lighting profile bundles the LightingMode + ReSTIRParams (incl. RELAX) + DDGIParams + ReflectionConfiguration + GTAOConfiguration + IBL intensity + the shading/lighting shader overrides; a post-process profile is a PostProcessConfiguration.
 * Load applies a profile into the live configs, Save writes the live configs out, and List fills a caller-provided array with the discovered profile names and returns the count.
 */
uint32_t ListLightingProfiles(ProfileName* outNames, uint32_t maxNames);

LightingProfileBundle CaptureLightingProfile(const EngineState& state);

void ApplyLightingProfile(EngineState& state, const LightingProfileBundle& bundle);

/** Overwrites only the members the stored profile contains, so seed @p bundle with CaptureLightingProfile first. */
bool LoadLightingProfile(const char* name, LightingProfileBundle& bundle);
bool SaveLightingProfile(const char* name, const LightingProfileBundle& bundle, Core::TlsfAllocator* alloc);
bool DeleteLightingProfile(const char* name);

uint32_t ListPostProcessProfiles(ProfileName* outNames, uint32_t maxNames);
bool LoadPostProcessProfile(const char* name, Core::PostProcessConfiguration& pp);
bool SavePostProcessProfile(const char* name, const Core::PostProcessConfiguration& pp, Core::TlsfAllocator* alloc);
bool DeletePostProcessProfile(const char* name);
}

#endif //WILL_ENGINE_PROFILE_LIBRARY_H
