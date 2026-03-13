//
// Created by William on 2026-03-13.
//

#ifndef WILL_ENGINE_MATERIAL_FORMAT_H
#define WILL_ENGINE_MATERIAL_FORMAT_H

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>

namespace Engine
{
constexpr uint32_t MATERIAL_MAJOR_VERSION = 1;
constexpr uint32_t MATERIAL_MINOR_VERSION = 0;
constexpr size_t WMATERIAL_NAME_LENGTH = 128;

struct WMaterialHeader
{
    uint64_t materialId{0};
    char name[WMATERIAL_NAME_LENGTH]{};
    uint32_t major{MATERIAL_MAJOR_VERSION};
    uint32_t minor{MATERIAL_MINOR_VERSION};
    uint64_t dataOffset{0};
};

bool WriteWMaterialHeader(std::ostream& out, const WMaterialHeader& header);

std::optional<WMaterialHeader> ReadWMaterialHeader(std::istream& in);

std::optional<WMaterialHeader> ReadWMaterialHeader(const std::filesystem::path& path);
} // Engine

#endif //WILL_ENGINE_MATERIAL_FORMAT_H
