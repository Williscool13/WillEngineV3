//
// Created by William on 2026-05-09.
//

#include "font_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWFontHeader(Core::Vector<std::byte>& out, const WFontHeader& header)
{
    AppendText(out, "wsfont\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "id %llu\n", header.fontId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "source_size_px %u\n", header.sourceSizePx);
    AppendTextF(out, "sdf_spread %u\n", header.sdfSpread);
    AppendTextF(out, "em_size 0x%08x\n", FloatBits(header.emSize));
    AppendTextF(out, "ascender 0x%08x\n", FloatBits(header.ascender));
    AppendTextF(out, "descender 0x%08x\n", FloatBits(header.descender));
    AppendTextF(out, "line_height 0x%08x\n", FloatBits(header.lineHeight));
    AppendTextF(out, "atlas_width %u\n", header.atlasWidth);
    AppendTextF(out, "atlas_height %u\n", header.atlasHeight);
    AppendTextF(out, "glyph_count %u\n", header.glyphCount);
    AppendTextF(out, "atlas_data_size %llu\n", header.atlasDataSize);
    AppendTextF(out, "atlas_uncompressed_size %llu\n", header.atlasUncompressedSize);
    AppendTextF(out, "atlas_compression %u\n", static_cast<uint32_t>(header.atlasCompressionType));
    AppendTextF(out, "contour_glyph_count %u\n", header.contourGlyphCount);
    AppendTextF(out, "contour_count %u\n", header.contourCount);
    AppendTextF(out, "edge_count %u\n", header.edgeCount);
    AppendText(out, "end_header\n");
    return true;
}

static void ComputeOffsets(WFontHeader& header, uint64_t headerEnd)
{
    header.glyphDataOffset = headerEnd;
    header.glyphContourRangeOffset = header.glyphDataOffset + static_cast<uint64_t>(header.glyphCount) * sizeof(WGlyphInfo);
    header.contourRangeOffset = header.glyphContourRangeOffset + static_cast<uint64_t>(header.contourGlyphCount) * sizeof(WGlyphContourRange);
    header.edgeDataOffset = header.contourRangeOffset + static_cast<uint64_t>(header.contourCount) * sizeof(WContourRange);
    header.atlasDataOffset = header.edgeDataOffset + static_cast<uint64_t>(header.edgeCount) * sizeof(WFontEdge);
}

static bool ParseFontHeaderFields(char* line, size_t lineBufSize, WFontHeader& header)
{
    if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + lineBufSize, header.fontId); }
    else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, line + lineBufSize, header.contentVersion); }
    else if (strncmp(line, "name ", 5) == 0) {
        const char* name = line + 5;
        const size_t copyLen = std::min(strlen(name), WFONT_NAME_LENGTH - 1);
        memcpy(header.name, name, copyLen);
        header.name[copyLen] = '\0';
    }
    else if (strncmp(line, "source_size_px ", 15) == 0) { std::from_chars(line + 15, line + lineBufSize, header.sourceSizePx); }
    else if (strncmp(line, "sdf_spread ", 11) == 0) { std::from_chars(line + 11, line + lineBufSize, header.sdfSpread); }
    else if (strncmp(line, "em_size ", 8) == 0) { header.emSize = ParseHexFloat(line + 8, line + lineBufSize); }
    else if (strncmp(line, "ascender ", 9) == 0) { header.ascender = ParseHexFloat(line + 9, line + lineBufSize); }
    else if (strncmp(line, "descender ", 10) == 0) { header.descender = ParseHexFloat(line + 10, line + lineBufSize); }
    else if (strncmp(line, "line_height ", 12) == 0) { header.lineHeight = ParseHexFloat(line + 12, line + lineBufSize); }
    else if (strncmp(line, "atlas_width ", 12) == 0) { std::from_chars(line + 12, line + lineBufSize, header.atlasWidth); }
    else if (strncmp(line, "atlas_height ", 13) == 0) { std::from_chars(line + 13, line + lineBufSize, header.atlasHeight); }
    else if (strncmp(line, "glyph_count ", 12) == 0) { std::from_chars(line + 12, line + lineBufSize, header.glyphCount); }
    else if (strncmp(line, "atlas_data_size ", 16) == 0) { std::from_chars(line + 16, line + lineBufSize, header.atlasDataSize); }
    else if (strncmp(line, "atlas_uncompressed_size ", 24) == 0) { std::from_chars(line + 24, line + lineBufSize, header.atlasUncompressedSize); }
    else if (strncmp(line, "atlas_compression ", 18) == 0) {
        uint32_t v = 0;
        std::from_chars(line + 18, line + lineBufSize, v);
        header.atlasCompressionType = static_cast<CompressionType>(v);
    }
    else if (strncmp(line, "contour_glyph_count ", 20) == 0) { std::from_chars(line + 20, line + lineBufSize, header.contourGlyphCount); }
    else if (strncmp(line, "contour_count ", 14) == 0) { std::from_chars(line + 14, line + lineBufSize, header.contourCount); }
    else if (strncmp(line, "edge_count ", 11) == 0) { std::from_chars(line + 11, line + lineBufSize, header.edgeCount); }
    return true;
}

static std::optional<WFontHeader> ReadWFontHeaderInternal(const void* data, uint64_t size, bool bAnyVersion)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wsfont") != 0) { return std::nullopt; }

    WFontHeader header{};
    bool bCompressionSeen = false;
    while (in.GetLine(line, LINE_BUF)) {
        if (strcmp(line, "end_header") == 0) {
            if (!bCompressionSeen) { return std::nullopt; }
            ComputeOffsets(header, in.offset);
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            auto res = std::from_chars(line + 8, line + LINE_BUF, header.major);
            if (res.ptr && *res.ptr == ' ') { std::from_chars(res.ptr + 1, line + LINE_BUF, header.minor); }
            if (!bAnyVersion && (header.major != FONT_MAJOR_VERSION || header.minor > FONT_MINOR_VERSION)) { return std::nullopt; }
        }
        else {
            if (strncmp(line, "atlas_compression ", 18) == 0) { bCompressionSeen = true; }
            ParseFontHeaderFields(line, LINE_BUF, header);
        }
    }
    return std::nullopt;
}

std::optional<WFontHeader> ReadWFontHeader(const void* data, uint64_t size)
{
    return ReadWFontHeaderInternal(data, size, false);
}

std::optional<WFontHeader> ReadWFontHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWFontHeaderInternal(map.data, map.size, false);
}

std::optional<WFontHeader> ReadWFontHeaderAnyVersion(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWFontHeaderInternal(map.data, map.size, true);
}
} // Engine
