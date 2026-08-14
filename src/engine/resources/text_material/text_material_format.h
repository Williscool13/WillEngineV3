//
// Created by William on 2026-05-14.
//

#ifndef WILL_ENGINE_TEXT_MATERIAL_FORMAT_H
#define WILL_ENGINE_TEXT_MATERIAL_FORMAT_H

#include <cstdint>
#include <optional>

#include "core/containers/inline_path.h"
#include "core/containers/vector.h"

namespace Engine
{
constexpr uint32_t TEXT_MATERIAL_MAJOR_VERSION = 2;
constexpr uint32_t TEXT_MATERIAL_MINOR_VERSION = 0;
constexpr size_t WTEXT_MATERIAL_NAME_LENGTH = 128;

struct WTextMaterialHeader
{
    uint64_t textMaterialId{0};
    char name[WTEXT_MATERIAL_NAME_LENGTH]{};
    uint32_t major{TEXT_MATERIAL_MAJOR_VERSION};
    uint32_t minor{TEXT_MATERIAL_MINOR_VERSION};
    uint64_t dataOffset{0};
};

bool WriteWTextMaterialHeader(Core::Vector<std::byte>& out, const WTextMaterialHeader& header);

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(const void* data, uint64_t size);

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_TEXT_MATERIAL_FORMAT_H
