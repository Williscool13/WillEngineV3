//
// Created by William on 2026-03-09.
//

#include "texture_format.h"

#include "engine/compression/compression.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

namespace Engine
{
bool WriteWTextureHeader(std::ostream& out, const WTextureHeader& header)
{
    out << "wtexture\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.textureId << "\n";
    out << "content_version " << header.contentVersion << "\n";
    out << "name " << header.name << "\n";
    out << "width " << header.width << "\n";
    out << "height " << header.height << "\n";
    out << "mips " << header.mipCount << "\n";
    out << "data_size " << header.dataSize << "\n";
    out << "uncompressed_size " << header.uncompressedSize << "\n";
    out << "compression " << static_cast<uint32_t>(header.compressionType) << "\n";
    if (header.category == TextureCategory::Model) {
        out << "category model\n";
    }
    else if (header.category == TextureCategory::Builtin) {
        out << "category builtin\n";
    }
    if (header.ownerModelId != 0) {
        out << "owner_model " << header.ownerModelId << "\n";
        out << "owner_image_index " << header.ownerImageIndex << "\n";
    }
    if (header.genSource[0] != '\0') {
        out << "gen_source " << header.genSource << "\n";
        out << "gen_format " << header.genFormat << "\n";
        out << "gen_mips " << (header.bGenMips ? 1 : 0) << "\n";
        out << "gen_flip_y " << (header.bGenFlipY ? 1 : 0) << "\n";
    }
    out << "end_header\n";
    return out.good();
}

static std::optional<WTextureHeader> ReadWTextureHeaderInternal(std::istream& in, bool bAnyVersion)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wtexture") != 0) { return std::nullopt; }

    WTextureHeader header{};
    bool bCompressionSeen = false;
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        if (strcmp(line, "end_header") == 0) {
            if (!bCompressionSeen) { return std::nullopt; }
            header.dataOffset = static_cast<uint64_t>(in.tellg());
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            auto res = std::from_chars(line + 8, line + LINE_BUF, header.major);
            if (res.ptr && *res.ptr == ' ') { std::from_chars(res.ptr + 1, line + LINE_BUF, header.minor); }
            if (!bAnyVersion && header.major != TEXTURE_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) {
            std::from_chars(line + 3, line + LINE_BUF, header.textureId);
        }
        else if (strncmp(line, "content_version ", 16) == 0) {
            std::from_chars(line + 16, line + LINE_BUF, header.contentVersion);
        }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WTEXTURE_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
        else if (strncmp(line, "width ", 6) == 0) { std::from_chars(line + 6, line + LINE_BUF, header.width); }
        else if (strncmp(line, "height ", 7) == 0) { std::from_chars(line + 7, line + LINE_BUF, header.height); }
        else if (strncmp(line, "mips ", 5) == 0) { std::from_chars(line + 5, line + LINE_BUF, header.mipCount); }
        else if (strncmp(line, "data_size ", 10) == 0) { std::from_chars(line + 10, line + LINE_BUF, header.dataSize); }
        else if (strncmp(line, "uncompressed_size ", 18) == 0) { std::from_chars(line + 18, line + LINE_BUF, header.uncompressedSize); }
        else if (strncmp(line, "compression ", 12) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 12, line + LINE_BUF, v);
            header.compressionType = static_cast<CompressionType>(v);
            bCompressionSeen = true;
        }
        else if (strncmp(line, "category ", 9) == 0) {
            const char* value = line + 9;
            if (strcmp(value, "model") == 0) { header.category = TextureCategory::Model; }
            else if (strcmp(value, "builtin") == 0) { header.category = TextureCategory::Builtin; }
            else { header.category = TextureCategory::Standalone; }
        }
        else if (strncmp(line, "owner_model ", 12) == 0) { std::from_chars(line + 12, line + LINE_BUF, header.ownerModelId); }
        else if (strncmp(line, "owner_image_index ", 18) == 0) { std::from_chars(line + 18, line + LINE_BUF, header.ownerImageIndex); }
        else if (strncmp(line, "model_owned ", 12) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 12, line + LINE_BUF, v);
            if (v != 0) { header.category = TextureCategory::Model; }
        }
        else if (strncmp(line, "ungenerated ", 12) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 12, line + LINE_BUF, v);
            header.bUngenerated = v != 0;
        }
        else if (strncmp(line, "gen_source ", 11) == 0) {
            const char* src = line + 11;
            const size_t copyLen = std::min(strlen(src), WTEXTURE_GEN_SOURCE_LENGTH - 1);
            memcpy(header.genSource, src, copyLen);
            header.genSource[copyLen] = '\0';
        }
        else if (strncmp(line, "gen_format ", 11) == 0) { std::from_chars(line + 11, line + LINE_BUF, header.genFormat); }
        else if (strncmp(line, "gen_mips ", 9) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 9, line + LINE_BUF, v);
            header.bGenMips = v != 0;
        }
        else if (strncmp(line, "gen_flip_y ", 11) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 11, line + LINE_BUF, v);
            header.bGenFlipY = v != 0;
        }
    }
    return std::nullopt;
}

std::optional<WTextureHeader> ReadWTextureHeader(std::istream& in)
{
    return ReadWTextureHeaderInternal(in, false);
}

std::optional<WTextureHeader> ReadWTextureHeader(const Core::Path& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    return ReadWTextureHeader(f);
}

std::optional<WTextureHeader> ReadWTextureHeaderAnyVersion(const Core::Path& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    return ReadWTextureHeaderInternal(f, true);
}
} // Engine
