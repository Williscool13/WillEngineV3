//
// Created by William on 2026-03-13.
//

#ifndef WILL_ENGINE_SCENE_FORMAT_H
#define WILL_ENGINE_SCENE_FORMAT_H

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>

#include "core/containers/inline_path.h"

namespace Engine
{
constexpr uint32_t SCENE_MAJOR_VERSION = 1;
constexpr uint32_t SCENE_MINOR_VERSION = 0;
constexpr size_t WSCENE_NAME_LENGTH = 128;

struct WSceneHeader
{
    uint64_t sceneId{0};
    char name[WSCENE_NAME_LENGTH]{};

    uint32_t major{SCENE_MAJOR_VERSION};
    uint32_t minor{SCENE_MINOR_VERSION};

    uint32_t entityCount{0};
    uint64_t dataOffset{0};
};

bool WriteWSceneHeader(std::ostream& out, const WSceneHeader& header);

std::optional<WSceneHeader> ReadWSceneHeader(std::istream& in);

std::optional<WSceneHeader> ReadWSceneHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_SCENE_FORMAT_H
