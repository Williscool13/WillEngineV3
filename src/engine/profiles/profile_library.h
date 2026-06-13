//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_PROFILE_LIBRARY_H
#define WILL_ENGINE_PROFILE_LIBRARY_H

#include <cstdint>

#include "core/containers/inline_string.h"

namespace Core
{
struct ReSTIRParams;
struct GTAOConfiguration;
struct PostProcessConfiguration;
}

namespace Engine::Profiles
{
inline constexpr uint32_t MAX_PROFILES = 64;

using ProfileName = Core::InlineString<64>;

/**
 * Named config presets stored one-per-file under <config>/profiles/{lighting,postprocess}/*.wprofile.
 * A lighting profile bundles ReSTIRParams (incl. RELAX) + GTAOConfiguration; a post-process profile is a PostProcessConfiguration.
 * Load applies a profile into the live configs, Save writes the live configs out, and List fills a caller-provided array with the discovered profile names and returns the count.
 */
uint32_t ListLightingProfiles(ProfileName* outNames, uint32_t maxNames);
bool LoadLightingProfile(const char* name, Core::ReSTIRParams& restir, Core::GTAOConfiguration& gtao);
bool SaveLightingProfile(const char* name, const Core::ReSTIRParams& restir, const Core::GTAOConfiguration& gtao);
bool DeleteLightingProfile(const char* name);

uint32_t ListPostProcessProfiles(ProfileName* outNames, uint32_t maxNames);
bool LoadPostProcessProfile(const char* name, Core::PostProcessConfiguration& pp);
bool SavePostProcessProfile(const char* name, const Core::PostProcessConfiguration& pp);
bool DeletePostProcessProfile(const char* name);
}

#endif //WILL_ENGINE_PROFILE_LIBRARY_H
