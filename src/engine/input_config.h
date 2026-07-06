//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_INPUT_CONFIG_H
#define WILL_ENGINE_INPUT_CONFIG_H

#include "core/containers/inline_vector.h"
#include "engine/engine_api.h"

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
    Core::InlineVector<InputBindingOverride, 64> overrides{};
};

InputConfig ReadInputConfig();

bool WriteInputConfig(const InputConfig& config);

void ApplyInputOverrides(InputState& input, const InputConfig& config);

InputConfig BuildInputConfigFromState(const InputState& input);
} // Engine

#endif //WILL_ENGINE_INPUT_CONFIG_H
