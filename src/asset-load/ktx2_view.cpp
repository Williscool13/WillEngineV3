//
// Created by William on 2026-08-11.
//

#include "ktx2_view.h"

#include <cassert>
#include <cstring>

#include "engine/logging/engine_log.h"
#include "render/vulkan/vk_helpers.h"

namespace AssetLoad
{
static constexpr uint8_t KTX2_IDENTIFIER[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

struct Ktx2Header
{
    uint8_t identifier[12];
    uint32_t vkFormat;
    uint32_t typeSize;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    uint32_t pixelDepth;
    uint32_t layerCount;
    uint32_t faceCount;
    uint32_t levelCount;
    uint32_t supercompressionScheme;
    uint32_t dfdByteOffset;
    uint32_t dfdByteLength;
    uint32_t kvdByteOffset;
    uint32_t kvdByteLength;
    uint64_t sgdByteOffset;
    uint64_t sgdByteLength;
};

static_assert(sizeof(Ktx2Header) == 80, "KTX2 header must match file layout");

struct Ktx2FileLevelEntry
{
    uint64_t byteOffset;
    uint64_t byteLength;
    uint64_t uncompressedByteLength;
};

static_assert(sizeof(Ktx2FileLevelEntry) == 24, "KTX2 level index entry must match file layout");

bool Ktx2View::Parse(const uint8_t* data, size_t size)
{
    if (size < sizeof(Ktx2Header)) {
        LOG_ERROR(Asset, "KTX2 parse: file smaller than header ({} bytes)", size);
        return false;
    }

    Ktx2Header header{};
    memcpy(&header, data, sizeof(header));

    if (memcmp(header.identifier, KTX2_IDENTIFIER, sizeof(KTX2_IDENTIFIER)) != 0) {
        LOG_ERROR(Asset, "KTX2 parse: bad file identifier");
        return false;
    }
    if (header.supercompressionScheme != 0) {
        LOG_ERROR(Asset, "KTX2 parse: supercompression scheme {} not supported (engine assets are scheme NONE)", header.supercompressionScheme);
        return false;
    }
    if (header.vkFormat == VK_FORMAT_UNDEFINED) {
        LOG_ERROR(Asset, "KTX2 parse: VK_FORMAT_UNDEFINED (Basis/UASTC no longer supported)");
        return false;
    }

    vkFormat = static_cast<VkFormat>(header.vkFormat);
    baseWidth = header.pixelWidth;
    baseHeight = header.pixelHeight;
    baseDepth = header.pixelDepth > 0 ? header.pixelDepth : 1;
    bArray = header.layerCount > 0;
    layerCount = header.layerCount > 0 ? header.layerCount : 1;
    faceCount = header.faceCount > 0 ? header.faceCount : 1;
    bCubemap = faceCount == 6;
    levelCount = header.levelCount > 0 ? header.levelCount : 1;

    if (levelCount > MAX_LEVELS) {
        LOG_ERROR(Asset, "KTX2 parse: {} mip levels exceeds MAX_LEVELS", levelCount);
        return false;
    }

    switch (vkFormat) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            blockWidth = 4;
            blockHeight = 4;
            blockBytes = 8;
            break;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            blockWidth = 4;
            blockHeight = 4;
            blockBytes = 16;
            break;
        default:
            blockWidth = 1;
            blockHeight = 1;
            blockBytes = Render::VkHelpers::GetBytesPerPixel(vkFormat);
            break;
    }

    const uint8_t* indexData = data + sizeof(Ktx2Header);
    if (size < sizeof(Ktx2Header) + static_cast<size_t>(levelCount) * sizeof(Ktx2FileLevelEntry)) {
        LOG_ERROR(Asset, "KTX2 parse: file truncated in level index");
        return false;
    }

    fileData = data;
    for (uint32_t i = 0; i < levelCount; i++) {
        Ktx2FileLevelEntry entry{};
        memcpy(&entry, indexData + static_cast<size_t>(i) * sizeof(Ktx2FileLevelEntry), sizeof(entry));
        if (entry.byteOffset + entry.byteLength > size) {
            LOG_ERROR(Asset, "KTX2 parse: level {} data out of bounds", i);
            return false;
        }
        levels[i] = {entry.byteOffset, entry.byteLength};
        assert(FaceSize(i) * faceCount * layerCount == entry.byteLength && "KTX2 level size does not match format math (unexpected padding?)");
    }

    return true;
}

uint64_t Ktx2View::FaceSize(uint32_t level) const
{
    const uint64_t blockRows = (LevelHeight(level) + blockHeight - 1) / blockHeight;
    return RowPitch(level) * blockRows * LevelDepth(level);
}

const uint8_t* Ktx2View::FaceData(uint32_t level, uint32_t face) const
{
    return fileData + levels[level].byteOffset + face * FaceSize(level);
}

uint64_t Ktx2View::RowPitch(uint32_t level) const
{
    const uint64_t blocksX = (LevelWidth(level) + blockWidth - 1) / blockWidth;
    return blocksX * blockBytes;
}
} // AssetLoad
