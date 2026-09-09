//
// Created by William on 2026-09-09.
//

#ifndef WILL_ENGINE_PLAY_FORMAT_H
#define WILL_ENGINE_PLAY_FORMAT_H

#include <cstdint>
#include <optional>

#include "core/containers/inline_path.h"

namespace Engine
{
constexpr uint32_t PLAY_MAJOR_VERSION = 1;
constexpr uint32_t PLAY_MINOR_VERSION = 0;
constexpr size_t WPLAY_NAME_LENGTH = 128;

/** .wplay header; the run binds to a scene by the scene's header name. */
struct WPlayHeader
{
    char name[WPLAY_NAME_LENGTH]{};
    char scene[WPLAY_NAME_LENGTH]{};

    uint32_t major{PLAY_MAJOR_VERSION};
    uint32_t minor{PLAY_MINOR_VERSION};

    uint64_t contentVersion{0};

    uint32_t eventCount{0};
    uint64_t dataOffset{0};
};

std::optional<WPlayHeader> ReadWPlayHeader(const void* data, uint64_t size);

std::optional<WPlayHeader> ReadWPlayHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_PLAY_FORMAT_H
