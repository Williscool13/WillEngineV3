//
// Created by William on 2026-08-11.
//

#ifndef WILL_ENGINE_KTX2_VIEW_H
#define WILL_ENGINE_KTX2_VIEW_H

#include <cstddef>
#include <cstdint>

#include <volk.h>

#include "core/containers/array.h"

namespace AssetLoad
{
/**
 * Zero-copy view over an in-memory KTX2 file. Supports supercompression NONE with a concrete VkFormat only (no Basis/UASTC transcoding).
 * Level data pointers reference the source buffer; it must outlive the view.
 */
class Ktx2View
{
public:
    static constexpr uint32_t MAX_LEVELS = 16;

    /**
     * Validates identifier, scheme, format, and level index bounds.
     * @param data complete KTX2 file in memory
     * @param size
     * @return false on any validation failure (logged)
     */
    bool Parse(const uint8_t* data, size_t size);

    /** Tightly packed byte size of a single face image at the given level. */
    [[nodiscard]] uint64_t FaceSize(uint32_t level) const;

    [[nodiscard]] const uint8_t* FaceData(uint32_t level, uint32_t face) const;

    [[nodiscard]] uint64_t RowPitch(uint32_t level) const;

    [[nodiscard]] uint32_t LevelWidth(uint32_t level) const { return baseWidth >> level > 0 ? baseWidth >> level : 1; }
    [[nodiscard]] uint32_t LevelHeight(uint32_t level) const { return baseHeight >> level > 0 ? baseHeight >> level : 1; }
    [[nodiscard]] uint32_t LevelDepth(uint32_t level) const { return baseDepth >> level > 0 ? baseDepth >> level : 1; }

    VkFormat vkFormat{VK_FORMAT_UNDEFINED};
    uint32_t baseWidth{0};
    uint32_t baseHeight{0};
    uint32_t baseDepth{1};
    uint32_t layerCount{1};
    uint32_t faceCount{1};
    uint32_t levelCount{1};
    bool bArray{false};
    bool bCubemap{false};

private:
    struct LevelEntry
    {
        uint64_t byteOffset{0};
        uint64_t byteLength{0};
    };

    const uint8_t* fileData{nullptr};
    Core::Array<LevelEntry, MAX_LEVELS> levels{};
    uint32_t blockWidth{1};
    uint32_t blockHeight{1};
    uint32_t blockBytes{4};
};
} // AssetLoad

#endif //WILL_ENGINE_KTX2_VIEW_H
