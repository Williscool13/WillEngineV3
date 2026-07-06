//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_INPUT_CONFIG_H
#define WILL_ENGINE_INPUT_CONFIG_H

#include "core/containers/inline_vector.h"
#include "engine/engine_api.h"
#include "engine/project_config.h"
#include "engine/profiles/profile_library.h"

namespace Engine
{
struct InputBindingOverride
{
    ActionHandle action;
    size_t bindingRowInDefault;
    BindingSource source;
};

struct InputConfig
{
    /** Sparse */
    Core::InlineVector<InputBindingOverride, 64> overrides{};
};

void ApplyInputOverrides(InputState& input, const InputConfig& config);

InputConfig BuildInputConfigFromState(const InputState& input);

namespace Profiles
{
uint32_t ListInputProfiles(ProfileName* outNames, uint32_t maxNames);
bool LoadInputProfile(const char* name, InputConfig& out);
bool SaveInputProfile(const char* name, const InputConfig& config);
bool DeleteInputProfile(const char* name);
} // Profiles

void LoadAndApplyInputConfig(InputState& input, const ProjectConfig& projectConfig);
void SaveInputConfig(const InputState& input, const ProjectConfig& projectConfig);
} // Engine

#endif //WILL_ENGINE_INPUT_CONFIG_H
