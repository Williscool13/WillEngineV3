//
// Created by William on 2026-05-09.
//

#ifndef WILL_ENGINE_FONT_FORMAT_H
#define WILL_ENGINE_FONT_FORMAT_H

#include <cstdint>
#include <optional>

#include "core/containers/inline_path.h"
#include "core/containers/vector.h"
#include "engine/compression/compression.h"

namespace Engine
{
constexpr uint32_t FONT_MAJOR_VERSION = 1;
constexpr uint32_t FONT_MINOR_VERSION = 0;
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

/**
 * Vector outline data for extruded 3D text. Only present when a font is imported with contour emission.
 * Coordinates are in the same EM space as WGlyphInfo planeBounds/advance.
 */
enum class WFontEdgeKind : uint32_t
{
    Linear = 0,
    Quadratic = 1,
    Cubic = 2,
};

/** One Bezier segment. p[0..1]=start, then 0/1/2 control points, then end; (x,y) pairs in EM units. */
struct WFontEdge
{
    WFontEdgeKind kind{WFontEdgeKind::Linear};
    float p[8]{};
};

struct WContourRange
{
    uint32_t firstEdge{0};
    uint32_t edgeCount{0};
};

struct WGlyphContourRange
{
    uint32_t firstContour{0};
    uint32_t contourCount{0};
};

struct WFontHeader
{
    uint64_t fontId{0};
    char name[WFONT_NAME_LENGTH]{};

    uint32_t major{FONT_MAJOR_VERSION};
    uint32_t minor{FONT_MINOR_VERSION};

    uint64_t contentVersion{0};

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
    /** Compressed size of the WImage atlas blob as stored on disk. */
    uint64_t atlasDataSize{0};
    /** Uncompressed size of the WImage atlas blob; used by the texture loader for decompression. */
    uint64_t atlasUncompressedSize{0};
    CompressionType atlasCompressionType{DEFAULT_FONT_COMPRESSION};

    /**
     * Contour outline tables for 3D text. contourGlyphCount==0 means no contours were baked (flat-only font).
     * When present, contourGlyphCount==glyphCount and the range table is indexed parallel to the glyph array.
     */
    uint32_t contourGlyphCount{0};
    uint32_t contourCount{0};
    uint32_t edgeCount{0};

    /**
     * Byte offsets from file start -- set by the reader after parsing end_header.
     * Layout: [header text] [glyphCount x WGlyphInfo] [contourGlyphCount x WGlyphContourRange] [contourCount x WContourRange] [edgeCount x WFontEdge] [atlas WImage blob]
     */
    uint64_t glyphDataOffset{0};
    uint64_t glyphContourRangeOffset{0};
    uint64_t contourRangeOffset{0};
    uint64_t edgeDataOffset{0};
    uint64_t atlasDataOffset{0};
};

bool WriteWFontHeader(Core::Vector<std::byte>& out, const WFontHeader& header);
std::optional<WFontHeader> ReadWFontHeader(const void* data, uint64_t size);
std::optional<WFontHeader> ReadWFontHeader(const Core::Path& path);
std::optional<WFontHeader> ReadWFontHeaderAnyVersion(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_FONT_FORMAT_H
