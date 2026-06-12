//
// Created by William on 2026-04-22.
//

#ifndef WILL_ENGINE_PROJECT_CONFIG_H
#define WILL_ENGINE_PROJECT_CONFIG_H

#include "core/containers/inline_string.h"
#include "render/interface/render_interface.h"

namespace Engine
{
struct ProjectConfig
{
    Core::InlineString<256> defaultScene{};
    bool bAutoSave{false};
    Core::LightingMode lightingMode{false};

    Core::ReSTIRParams restir{};
    Core::RELAXParams relax{};

    Core::AntiAliasingMode aaMode{Core::AntiAliasingMode::TAA};
    Core::GTAOConfiguration gtaoConfig{};
    Core::SMAAConfiguration smaaConfig{};
    Core::TAAConfiguration taaConfig{};
    Core::PostProcessConfiguration postProcess{};
};

/**
 * Reads project.wconfig from the project root (parent of assets/).
 * @return a zeroed config if the file doesn't exist or fails to parse.
 */
ProjectConfig ReadProjectConfig();

/**
 * Writes project.wconfig to the project root (parent of assets/).
 * @param config
 * @return Returns true on success.
 */
bool WriteProjectConfig(const ProjectConfig& config);
} // Engine

#endif //WILL_ENGINE_PROJECT_CONFIG_H
