//
// Created by William on 2026-03-09.
//

#ifndef WILL_ENGINE_TEXTURE_FORMAT_H
#define WILL_ENGINE_TEXTURE_FORMAT_H

#include <cstdint>
#include <iosfwd>
#include <optional>

#include "core/containers/inline_path.h"
#include "engine/compression/compression.h"

namespace Engine
{
constexpr uint32_t TEXTURE_MAJOR_VERSION = 0;
constexpr uint32_t TEXTURE_MINOR_VERSION = 5;
constexpr size_t WTEXTURE_NAME_LENGTH = 128;

struct WTextureHeader
{
    uint64_t textureId{0};
    char name[WTEXTURE_NAME_LENGTH]{};

    uint32_t major{TEXTURE_MAJOR_VERSION};
    uint32_t minor{TEXTURE_MINOR_VERSION};

    uint64_t contentVersion{0};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t mipCount{0};
    uint64_t dataOffset{0};
    uint64_t dataSize{0};
    uint64_t uncompressedSize{0};
    CompressionType compressionType{DEFAULT_TEXTURE_COMPRESSION};
};

bool WriteWTextureHeader(std::ostream& out, const WTextureHeader& header);

std::optional<WTextureHeader> ReadWTextureHeader(std::istream& in);

std::optional<WTextureHeader> ReadWTextureHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_TEXTURE_FORMAT_H
