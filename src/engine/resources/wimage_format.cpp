//
// Created by William on 2026-08-12.
//

#include "wimage_format.h"

#include <cstring>

#include "engine/logging/engine_log.h"
#include "render/vulkan/vk_helpers.h"

namespace Engine
{
struct BlockInfo
{
    uint32_t blockWidth{1};
    uint32_t blockHeight{1};
    uint32_t blockBytes{0};
};

static BlockInfo GetBlockInfo(VkFormat format)
{
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return {4, 4, 8};
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
            return {4, 4, 16};
        default:
            return {1, 1, Render::VkHelpers::GetBytesPerPixel(format)};
    }
}

static WImageLevel ComputeLevel(const WImageDesc& desc, const BlockInfo& block, uint32_t level)
{
    const uint32_t width = desc.width >> level > 0 ? desc.width >> level : 1;
    const uint32_t height = desc.height >> level > 0 ? desc.height >> level : 1;
    const uint64_t rowPitch = static_cast<uint64_t>((width + block.blockWidth - 1) / block.blockWidth) * block.blockBytes;
    const uint64_t blockRows = (height + block.blockHeight - 1) / block.blockHeight;
    return {0, rowPitch, rowPitch * blockRows};
}

static bool ValidateDesc(const WImageDesc& desc)
{
    return desc.width > 0 && desc.height > 0
           && desc.levelCount > 0 && desc.levelCount <= WIMAGE_MAX_LEVELS
           && desc.faceCount > 0
           && GetBlockInfo(desc.vkFormat).blockBytes > 0;
}

size_t WImageBlobSize(const WImageDesc& desc)
{
    if (!ValidateDesc(desc)) {
        return 0;
    }
    const BlockInfo block = GetBlockInfo(desc.vkFormat);
    size_t offset = sizeof(WImageHeader) + desc.levelCount * sizeof(WImageLevel);
    for (uint32_t level = 0; level < desc.levelCount; level++) {
        offset = (offset + WIMAGE_DATA_ALIGN - 1) & ~static_cast<size_t>(WIMAGE_DATA_ALIGN - 1);
        offset += ComputeLevel(desc, block, level).faceSize * desc.faceCount;
    }
    return offset;
}

size_t WImageBlobInit(uint8_t* dst, size_t dstSize, const WImageDesc& desc)
{
    const size_t totalSize = WImageBlobSize(desc);
    if (totalSize == 0 || totalSize > dstSize) {
        LOG_ERROR(Asset, "WImage blob init failed (size {} vs capacity {})", totalSize, dstSize);
        return 0;
    }

    WImageHeader header{};
    header.vkFormat = static_cast<uint32_t>(desc.vkFormat);
    header.width = desc.width;
    header.height = desc.height;
    header.levelCount = desc.levelCount;
    header.faceCount = desc.faceCount;
    memcpy(dst, &header, sizeof(header));

    const BlockInfo block = GetBlockInfo(desc.vkFormat);
    size_t offset = sizeof(WImageHeader) + desc.levelCount * sizeof(WImageLevel);
    for (uint32_t level = 0; level < desc.levelCount; level++) {
        offset = (offset + WIMAGE_DATA_ALIGN - 1) & ~static_cast<size_t>(WIMAGE_DATA_ALIGN - 1);
        WImageLevel entry = ComputeLevel(desc, block, level);
        entry.byteOffset = offset;
        memcpy(dst + sizeof(WImageHeader) + level * sizeof(WImageLevel), &entry, sizeof(entry));
        offset += entry.faceSize * desc.faceCount;
    }
    return totalSize;
}

static WImageLevel ReadLevelEntry(const uint8_t* blob, uint32_t level)
{
    WImageLevel entry{};
    memcpy(&entry, blob + sizeof(WImageHeader) + level * sizeof(WImageLevel), sizeof(entry));
    return entry;
}

uint8_t* WImageFaceData(uint8_t* blob, uint32_t level, uint32_t face)
{
    const WImageLevel entry = ReadLevelEntry(blob, level);
    return blob + entry.byteOffset + face * entry.faceSize;
}

uint64_t WImageFaceSize(const uint8_t* blob, uint32_t level)
{
    return ReadLevelEntry(blob, level).faceSize;
}

bool WImageView::Parse(const uint8_t* data, size_t size)
{
    if (size < sizeof(WImageHeader)) {
        LOG_ERROR(Asset, "WImage parse: blob smaller than header ({} bytes)", size);
        return false;
    }

    WImageHeader header{};
    memcpy(&header, data, sizeof(header));

    if (header.magic != WIMAGE_MAGIC) {
        LOG_ERROR(Asset, "WImage parse: bad magic");
        return false;
    }
    if (header.version != WIMAGE_VERSION) {
        LOG_ERROR(Asset, "WImage parse: version {} not supported", header.version);
        return false;
    }
    if (header.vkFormat == VK_FORMAT_UNDEFINED) {
        LOG_ERROR(Asset, "WImage parse: VK_FORMAT_UNDEFINED");
        return false;
    }
    if (header.levelCount == 0 || header.levelCount > WIMAGE_MAX_LEVELS) {
        LOG_ERROR(Asset, "WImage parse: invalid level count {}", header.levelCount);
        return false;
    }
    if (header.faceCount == 0 || header.width == 0 || header.height == 0) {
        LOG_ERROR(Asset, "WImage parse: invalid dimensions {}x{} faces {}", header.width, header.height, header.faceCount);
        return false;
    }
    if (size < sizeof(WImageHeader) + static_cast<size_t>(header.levelCount) * sizeof(WImageLevel)) {
        LOG_ERROR(Asset, "WImage parse: blob truncated in level table");
        return false;
    }

    vkFormat = static_cast<VkFormat>(header.vkFormat);
    baseWidth = header.width;
    baseHeight = header.height;
    levelCount = header.levelCount;
    faceCount = header.faceCount;
    bCubemap = faceCount == 6;
    fileData = data;

    for (uint32_t i = 0; i < levelCount; i++) {
        const WImageLevel entry = ReadLevelEntry(data, i);
        if (entry.rowPitch == 0 || entry.faceSize == 0 || entry.faceSize % entry.rowPitch != 0) {
            LOG_ERROR(Asset, "WImage parse: level {} has invalid pitch/size", i);
            return false;
        }
        if (entry.byteOffset + entry.faceSize * faceCount > size) {
            LOG_ERROR(Asset, "WImage parse: level {} data out of bounds", i);
            return false;
        }
        levels[i] = entry;
    }

    return true;
}
} // Engine
