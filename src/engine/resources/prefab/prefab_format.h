//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PREFAB_FORMAT_H
#define WILL_ENGINE_PREFAB_FORMAT_H

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>

#include <json/nlohmann/json.hpp>

namespace Engine
{
constexpr uint32_t PREFAB_MAJOR_VERSION = 1;
constexpr uint32_t PREFAB_MINOR_VERSION = 0;
constexpr size_t WPREFAB_NAME_LENGTH = 128;

struct WPrefabHeader
{
    uint64_t prefabId{0};
    char name[WPREFAB_NAME_LENGTH]{};

    uint32_t major{PREFAB_MAJOR_VERSION};
    uint32_t minor{PREFAB_MINOR_VERSION};

    uint32_t componentCount{0};
    uint64_t dataOffset{0};
};

bool WriteWPrefabHeader(std::ostream& out, const WPrefabHeader& header);

std::optional<WPrefabHeader> ReadWPrefabHeader(std::istream& in);

std::optional<WPrefabHeader> ReadWPrefabHeader(const std::filesystem::path& path);

struct WPrefabData
{
    WPrefabHeader header;
    nlohmann::json componentJson;
};

std::optional<WPrefabData> ReadWPrefab(const std::filesystem::path& path);
} // Engine

#endif //WILL_ENGINE_PREFAB_FORMAT_H
