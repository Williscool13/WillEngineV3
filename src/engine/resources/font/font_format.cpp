//
// Created by William on 2026-05-09.
//

#include "font_format.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

namespace Engine
{
bool WriteWFontHeader(std::ostream& out, const WFontHeader& header)
{
    out << "wsfont\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.fontId << "\n";
    out << "name " << header.name << "\n";
    out << "source_size_px " << header.sourceSizePx << "\n";
    out << "sdf_spread " << header.sdfSpread << "\n";
    out << "em_size " << header.emSize << "\n";
    out << "ascender " << header.ascender << "\n";
    out << "descender " << header.descender << "\n";
    out << "line_height " << header.lineHeight << "\n";
    out << "atlas_width " << header.atlasWidth << "\n";
    out << "atlas_height " << header.atlasHeight << "\n";
    out << "glyph_count " << header.glyphCount << "\n";
    out << "atlas_data_size " << header.atlasDataSize << "\n";
    out << "atlas_uncompressed_size " << header.atlasUncompressedSize << "\n";
    out << "atlas_compression " << static_cast<uint32_t>(header.atlasCompressionType) << "\n";
    out << "end_header\n";
    return out.good();
}

static void ComputeOffsets(WFontHeader& header, uint64_t headerEnd)
{
    header.glyphDataOffset = headerEnd;
    header.atlasDataOffset = header.glyphDataOffset + static_cast<uint64_t>(header.glyphCount) * sizeof(WGlyphInfo);
}

static bool ParseFontHeaderFields(char* line, size_t lineBufSize, WFontHeader& header)
{
    if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + lineBufSize, header.fontId); }
    else if (strncmp(line, "name ", 5) == 0) {
        const char* name = line + 5;
        const size_t copyLen = std::min(strlen(name), WFONT_NAME_LENGTH - 1);
        memcpy(header.name, name, copyLen);
        header.name[copyLen] = '\0';
    }
    else if (strncmp(line, "source_size_px ", 15) == 0) { std::from_chars(line + 15, line + lineBufSize, header.sourceSizePx); }
    else if (strncmp(line, "sdf_spread ", 11) == 0) { std::from_chars(line + 11, line + lineBufSize, header.sdfSpread); }
    else if (strncmp(line, "em_size ", 8) == 0) { std::from_chars(line + 8, line + lineBufSize, header.emSize); }
    else if (strncmp(line, "ascender ", 9) == 0) { std::from_chars(line + 9, line + lineBufSize, header.ascender); }
    else if (strncmp(line, "descender ", 10) == 0) { std::from_chars(line + 10, line + lineBufSize, header.descender); }
    else if (strncmp(line, "line_height ", 12) == 0) { std::from_chars(line + 12, line + lineBufSize, header.lineHeight); }
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
    return true;
}

std::optional<WFontHeader> ReadWFontHeader(std::istream& in)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wsfont") != 0) { return std::nullopt; }

    WFontHeader header{};
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        if (strcmp(line, "end_header") == 0) {
            ComputeOffsets(header, static_cast<uint64_t>(in.tellg()));
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0, minor = 0;
            auto res = std::from_chars(line + 8, line + LINE_BUF, major);
            if (res.ptr && *res.ptr == ' ') { std::from_chars(res.ptr + 1, line + LINE_BUF, minor); }
            if (major != FONT_MAJOR_VERSION || minor != FONT_MINOR_VERSION) { return std::nullopt; }
        }
        else { ParseFontHeaderFields(line, LINE_BUF, header); }
    }
    return std::nullopt;
}

std::optional<WFontHeader> ReadWFontHeader(const Core::Path& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) { return std::nullopt; }
    return ReadWFontHeader(file);
}

std::optional<WFontHeader> ReadWFontHeaderAnyVersion(const Core::Path& path)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) { return std::nullopt; }

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wsfont") != 0) { return std::nullopt; }

    WFontHeader header{};
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        if (strcmp(line, "end_header") == 0) {
            ComputeOffsets(header, static_cast<uint64_t>(in.tellg()));
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) { std::from_chars(line + 8, line + LINE_BUF, header.major); }
        else { ParseFontHeaderFields(line, LINE_BUF, header); }
    }
    return std::nullopt;
}
} // Engine
