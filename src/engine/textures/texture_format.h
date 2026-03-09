//
// Created by William on 2026-03-09.
//

#ifndef WILL_ENGINE_TEXTURE_FORMAT_H
#define WILL_ENGINE_TEXTURE_FORMAT_H

#include <cstdint>

namespace Engine
{
constexpr char WILL_TEXTURE_MAGIC[8] = "WILLTEX";
constexpr uint32_t TEXTURE_MAJOR_VERSION = 0;
constexpr uint32_t TEXTURE_MINOR_VERSION = 1;

constexpr size_t WTEXTURE_NAME_LENGTH = 128;

struct WTextureHeader
{
    char     magic[8];
    uint32_t majorVersion;
    uint32_t minorVersion;
    uint64_t textureId;
    char     name[WTEXTURE_NAME_LENGTH];
    uint32_t width;
    uint32_t height;
    uint32_t mipCount;
    uint32_t dataOffset;  // Byte offset to the KTX2 blob (always sizeof(WTextureHeader)).
    uint64_t dataSize;    // Size of the KTX2 blob in bytes.
};
} // Engine

#endif //WILL_ENGINE_TEXTURE_FORMAT_H
