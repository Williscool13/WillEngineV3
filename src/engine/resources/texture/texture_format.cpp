//
// Created by William on 2026-03-09.
//

#include "texture_format.h"

#include <charconv>
#include <cstring>

#include "engine/compression/compression.h"
#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWTextureHeader(Core::Vector<std::byte>& out, const WTextureHeader& header)
{
    AppendText(out, "wtexture\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "id %llu\n", header.textureId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "width %u\n", header.width);
    AppendTextF(out, "height %u\n", header.height);
    AppendTextF(out, "mips %u\n", header.mipCount);
    AppendTextF(out, "data_size %llu\n", header.dataSize);
    AppendTextF(out, "uncompressed_size %llu\n", header.uncompressedSize);
    AppendTextF(out, "compression %u\n", static_cast<uint32_t>(header.compressionType));
    if (header.category == TextureCategory::Model) {
        AppendText(out, "category model\n");
    }
    else if (header.category == TextureCategory::Builtin) {
        AppendText(out, "category builtin\n");
    }
    if (header.ownerModelId != 0) {
        AppendTextF(out, "owner_model %llu\n", header.ownerModelId);
        AppendTextF(out, "owner_image_index %u\n", header.ownerImageIndex);
    }
    if (header.genSource[0] != '\0') {
        AppendTextF(out, "gen_source %s\n", header.genSource);
        AppendTextF(out, "gen_format %u\n", header.genFormat);
        AppendTextF(out, "gen_mips %u\n", header.bGenMips ? 1 : 0);
        AppendTextF(out, "gen_flip_y %u\n", header.bGenFlipY ? 1 : 0);
    }
    AppendText(out, "end_header\n");
    return true;
}

static std::optional<WTextureHeader> ReadWTextureHeaderInternal(const void* data, uint64_t size, bool bAnyVersion)
{
    constexpr size_t LINE_BUF = 512;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wtexture") != 0) { return std::nullopt; }

    WTextureHeader header{};
    bool bCompressionSeen = false;
    while (in.GetLine(line, LINE_BUF)) {
        if (strcmp(line, "end_header") == 0) {
            if (!bCompressionSeen) { return std::nullopt; }
            header.dataOffset = in.offset;
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

std::optional<WTextureHeader> ReadWTextureHeader(const void* data, uint64_t size)
{
    return ReadWTextureHeaderInternal(data, size, false);
}

std::optional<WTextureHeader> ReadWTextureHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWTextureHeaderInternal(map.data, map.size, false);
}

std::optional<WTextureHeader> ReadWTextureHeaderAnyVersion(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWTextureHeaderInternal(map.data, map.size, true);
}
} // Engine
