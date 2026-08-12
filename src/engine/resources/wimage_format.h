//
// Created by William on 2026-08-12.
//

#ifndef WILL_ENGINE_WIMAGE_FORMAT_H
#define WILL_ENGINE_WIMAGE_FORMAT_H

#include <cstddef>
#include <cstdint>

#include <volk.h>

#include "core/containers/array.h"

namespace Engine
{
constexpr uint32_t WIMAGE_MAGIC = 0x474D4957; // "WIMG"
constexpr uint32_t WIMAGE_VERSION = 1;
constexpr uint32_t WIMAGE_MAX_LEVELS = 16;
constexpr uint32_t WIMAGE_DATA_ALIGN = 16;

/**
 * Image payload blob embedded (zstd-compressed) inside .wtexture/.wenvmap/.wprobe/.wsfont files.
 * Layout: [WImageHeader] [levelCount x WImageLevel] [aligned level data, faces tightly packed within each level].
 * rowPitch and faceSize are baked at write time so readers need no per-format math.
 */
struct WImageHeader
{
    uint32_t magic{WIMAGE_MAGIC};
    uint32_t version{WIMAGE_VERSION};
    uint32_t vkFormat{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t levelCount{1};
    uint32_t faceCount{1};
    uint32_t reserved{0};
};

struct WImageLevel
{
    uint64_t byteOffset{0};
    uint64_t rowPitch{0};
    uint64_t faceSize{0};
};

struct WImageDesc
{
    VkFormat vkFormat{VK_FORMAT_UNDEFINED};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t levelCount{1};
    uint32_t faceCount{1};
};

size_t WImageBlobSize(const WImageDesc& desc);

/**
 * Writes the header and level table into dst and returns the total blob size; 0 on failure.
 * Level data regions are left uninitialized; fill them via WImageFaceData. Distinct (level, face) regions may be filled from different threads.
 */
size_t WImageBlobInit(uint8_t* dst, size_t dstSize, const WImageDesc& desc);

/**
 * Mutable pointer to a face's data region inside a blob initialized by WImageBlobInit.
 * @param blob
 * @param level
 * @param face
 * @return
 */
uint8_t* WImageFaceData(uint8_t* blob, uint32_t level, uint32_t face);


/**
 * Byte size of one face at the given level inside an initialized blob.
 * @param blob
 * @param level
 * @return
 */
uint64_t WImageFaceSize(const uint8_t* blob, uint32_t level);

class WImageView
{
public:
    /**
     * Validates magic, version, counts, and level table bounds.
     * @param data
     * @param size
     * @return false on any validation failure (logged)
     */
    bool Parse(const uint8_t* data, size_t size);

    [[nodiscard]] uint64_t FaceSize(uint32_t level) const { return levels[level].faceSize; }

    [[nodiscard]] const uint8_t* FaceData(uint32_t level, uint32_t face) const { return fileData + levels[level].byteOffset + face * levels[level].faceSize; }

    [[nodiscard]] uint64_t RowPitch(uint32_t level) const { return levels[level].rowPitch; }

    [[nodiscard]] uint32_t LevelWidth(uint32_t level) const { return baseWidth >> level > 0 ? baseWidth >> level : 1; }
    [[nodiscard]] uint32_t LevelHeight(uint32_t level) const { return baseHeight >> level > 0 ? baseHeight >> level : 1; }

    VkFormat vkFormat{VK_FORMAT_UNDEFINED};
    uint32_t baseWidth{0};
    uint32_t baseHeight{0};
    uint32_t levelCount{1};
    uint32_t faceCount{1};
    bool bCubemap{false};

private:
    const uint8_t* fileData{nullptr};
    Core::Array<WImageLevel, WIMAGE_MAX_LEVELS> levels{};
};
} // Engine

#endif //WILL_ENGINE_WIMAGE_FORMAT_H
