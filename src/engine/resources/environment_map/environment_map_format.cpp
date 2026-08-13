//
// Created by William on 2026-04-15.
//

#include "environment_map_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWEnvMapHeader(Core::Vector<std::byte>& out, const WEnvMapHeader& header)
{
    AppendText(out, "wenvmap\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "id %llu\n", header.environmentMapId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "width %u\n", header.width);
    AppendTextF(out, "height %u\n", header.height);
    AppendTextF(out, "mips %u\n", header.mipCount);
    AppendTextF(out, "data_size %llu\n", header.dataSize);
    AppendTextF(out, "uncompressed_size %llu\n", header.uncompressedSize);
    AppendTextF(out, "compression %u\n", static_cast<uint32_t>(header.compressionType));
    AppendText(out, "end_header\n");
    return true;
}

static std::optional<WEnvMapHeader> ReadWEnvMapHeaderInternal(const void* data, uint64_t size, bool bAnyVersion)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wenvmap") != 0) { return std::nullopt; }

    WEnvMapHeader header{};
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
            if (!bAnyVersion && header.major != ENV_MAP_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) {
            std::from_chars(line + 3, line + LINE_BUF, header.environmentMapId);
        }
        else if (strncmp(line, "content_version ", 16) == 0) {
            std::from_chars(line + 16, line + LINE_BUF, header.contentVersion);
        }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WENVMAP_NAME_LENGTH - 1);
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
    }
    return std::nullopt;
}

std::optional<WEnvMapHeader> ReadWEnvMapHeader(const void* data, uint64_t size)
{
    return ReadWEnvMapHeaderInternal(data, size, false);
}

std::optional<WEnvMapHeader> ReadWEnvMapHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWEnvMapHeaderInternal(map.data, map.size, false);
}

std::optional<WEnvMapHeader> ReadWEnvMapHeaderAnyVersion(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWEnvMapHeaderInternal(map.data, map.size, true);
}
} // Engine
