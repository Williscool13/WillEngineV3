//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_PROFILE_LIBRARY_H
#define WILL_ENGINE_PROFILE_LIBRARY_H

#include <cstdint>

#include "core/containers/inline_string.h"
#include "core/string_id.h"

namespace Core
{
struct ReSTIRParams;
struct DDGIParams;
struct RTReflectionConfiguration;
struct HeroShadowConfiguration;
struct GTAOConfiguration;
struct PostProcessConfiguration;
enum class LightingMode : uint8_t;
}

namespace Engine::Profiles
{
inline constexpr uint32_t MAX_PROFILES = 64;

using ProfileName = Core::InlineString<64>;

/**
 * Named config presets stored one-per-file under <config>/profiles/{lighting,postprocess}/*.wprofile.
 * A lighting profile bundles the LightingMode + ReSTIRParams (incl. RELAX) + DDGIParams + RTReflectionConfiguration + GTAOConfiguration + HeroShadowConfiguration + IBL intensity + the shading/lighting shader overrides; a post-process profile is a PostProcessConfiguration.
 * Load applies a profile into the live configs, Save writes the live configs out, and List fills a caller-provided array with the discovered profile names and returns the count.
 */
uint32_t ListLightingProfiles(ProfileName* outNames, uint32_t maxNames);
bool LoadLightingProfile(const char* name, Core::LightingMode& lightingMode, Core::ReSTIRParams& restir, Core::DDGIParams& ddgi, Core::RTReflectionConfiguration& reflection, Core::GTAOConfiguration& gtao, Core::HeroShadowConfiguration& heroShadow, StringID& shadingOverride, StringID& lightingOverride, float& iblIntensity);
bool SaveLightingProfile(const char* name, Core::LightingMode lightingMode, const Core::ReSTIRParams& restir, const Core::DDGIParams& ddgi, const Core::RTReflectionConfiguration& reflection, const Core::GTAOConfiguration& gtao, const Core::HeroShadowConfiguration& heroShadow, StringID shadingOverride, StringID lightingOverride, float iblIntensity);
bool DeleteLightingProfile(const char* name);

uint32_t ListPostProcessProfiles(ProfileName* outNames, uint32_t maxNames);
bool LoadPostProcessProfile(const char* name, Core::PostProcessConfiguration& pp);
bool SavePostProcessProfile(const char* name, const Core::PostProcessConfiguration& pp);
bool DeletePostProcessProfile(const char* name);
}

#endif //WILL_ENGINE_PROFILE_LIBRARY_H
