//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_AUTOMATION_CONFIG_H
#define WILL_ENGINE_AUTOMATION_CONFIG_H

#include <cstdint>

#include "core/containers/inline_string.h"

namespace Engine
{
/** Command-line automation options, carried on EngineState so game.dll can drive a scripted run. */
struct AutomationConfig
{
    Core::InlineString<256> sceneOverride{};
    Core::InlineString<512> playPath{};
    Core::InlineString<512> outputDir{};
    bool bExitWhenDone{false};
    bool bForceNoREBAR{false};
    int32_t mcpPort{0};

    [[nodiscard]] bool IsPlayRun() const { return !playPath.IsEmpty(); }
};
} // Engine

#endif //WILL_ENGINE_AUTOMATION_CONFIG_H
