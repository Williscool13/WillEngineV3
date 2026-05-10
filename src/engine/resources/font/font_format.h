//
// Created by William on 2026-05-09.
//

#ifndef WILL_ENGINE_FONT_FORMAT_H
#define WILL_ENGINE_FONT_FORMAT_H

#include <cstdint>
#include <iosfwd>
#include <optional>

#include "core/containers/inline_path.h"

namespace Engine
{
constexpr uint32_t FONT_MAJOR_VERSION = 0;
constexpr uint32_t FONT_MINOR_VERSION = 1;
constexpr size_t WFONT_NAME_LENGTH = 128;

/**
 * Per-glyph metrics stored in the .wsfont body.
 * planeBounds: quad rect relative to cursor in EM units (left, bottom, right, top).
 * uvBounds: normalized atlas UVs [0,1], V already flipped for Vulkan (y-down).
 */
struct WGlyphInfo
{
    uint32_t codepoint{0};
    float advance{0.0f};

    float planeLeft{0.0f};
    float planeBottom{0.0f};
    float planeRight{0.0f};
    float planeTop{0.0f};

    float uvLeft{0.0f};
    float uvBottom{0.0f};
    float uvRight{0.0f};
    float uvTop{0.0f};
};

struct WKerningPair
{
    uint32_t codepoint0{0};
    uint32_t codepoint1{0};
    float advance{0.0f};
};

struct WFontHeader
{
    uint64_t fontId{0};
    char name[WFONT_NAME_LENGTH]{};

    uint32_t major{FONT_MAJOR_VERSION};
    uint32_t minor{FONT_MINOR_VERSION};

    /** Source rasterization size in pixels (e.g. 48). */
    uint32_t sourceSizePx{0};
    /** SDF texel spread radius in pixels. Used in shader smoothstep edge computation. */
    uint32_t sdfSpread{0};

    /** EM square size; used to convert EM-unit planeBounds to screen pixels: px = emValue * (renderSizePx / emSize). */
    float emSize{1.0f};
    float ascender{0.0f};
    float descender{0.0f};
    float lineHeight{0.0f};

    uint32_t atlasWidth{0};
    uint32_t atlasHeight{0};
    uint32_t glyphCount{0};
    uint32_t kerningCount{0};
    uint64_t atlasDataSize{0};

    /**
     * Byte offsets from file start -- set by the reader after parsing end_header.
     * Layout: [header text] [glyphCount x WGlyphInfo] [kerningCount x WKerningPair] [atlas KTX2 blob]
     */
    uint64_t glyphDataOffset{0};
    uint64_t kerningDataOffset{0};
    uint64_t atlasDataOffset{0};
};

bool WriteWFontHeader(std::ostream& out, const WFontHeader& header);

std::optional<WFontHeader> ReadWFontHeader(std::istream& in);
std::optional<WFontHeader> ReadWFontHeader(const Core::Path& path);

/** Reads header without rejecting on version mismatch. For use by the asset generator to detect stale files. */
std::optional<WFontHeader> ReadWFontHeaderAnyVersion(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_FONT_FORMAT_H
