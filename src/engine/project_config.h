//
// Created by William on 2026-04-22.
//

#ifndef WILL_ENGINE_PROJECT_CONFIG_H
#define WILL_ENGINE_PROJECT_CONFIG_H

#include "core/containers/inline_string.h"

namespace Engine
{
struct ProjectConfig
{
    Core::InlineString<256> defaultScene{};
};

// Reads project.wconfig from the project root (parent of assets/).
// Returns a zeroed config if the file doesn't exist or fails to parse.
ProjectConfig ReadProjectConfig();

// Writes project.wconfig to the project root (parent of assets/).
// Returns true on success.
bool WriteProjectConfig(const ProjectConfig& config);
} // Engine

#endif //WILL_ENGINE_PROJECT_CONFIG_H
