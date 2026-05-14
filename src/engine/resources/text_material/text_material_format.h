//
// Created by William on 2026-05-14.
//

#ifndef WILL_ENGINE_TEXT_MATERIAL_FORMAT_H
#define WILL_ENGINE_TEXT_MATERIAL_FORMAT_H

#include <cstdint>
#include <iosfwd>
#include <optional>

#include "core/containers/inline_path.h"

namespace Engine
{
constexpr uint32_t TEXT_MATERIAL_MAJOR_VERSION = 1;
constexpr uint32_t TEXT_MATERIAL_MINOR_VERSION = 0;
constexpr size_t WTEXT_MATERIAL_NAME_LENGTH = 128;

struct WTextMaterialHeader
{
    uint64_t textMaterialId{0};
    char name[WTEXT_MATERIAL_NAME_LENGTH]{};
    uint32_t major{TEXT_MATERIAL_MAJOR_VERSION};
    uint32_t minor{TEXT_MATERIAL_MINOR_VERSION};
};

bool WriteWTextMaterialHeader(std::ostream& out, const WTextMaterialHeader& header);

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(std::istream& in);

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_TEXT_MATERIAL_FORMAT_H
