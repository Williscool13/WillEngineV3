//
// Created by William on 2026-10-09.
//

#include "shadow_map_format.h"

#include <bit>
#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
static void ParseFloats(const char* s, const char* end, float* out, int32_t count)
{
    for (int32_t i = 0; i < count; ++i) {
        while (s < end && (*s == ' ' || *s == '\t')) { ++s; }
        if (end - s > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; }
        uint32_t bits = 0;
        const auto result = std::from_chars(s, end, bits, 16);
        if (result.ec != std::errc{}) { return; }
        out[i] = std::bit_cast<float>(bits);
        s = result.ptr;
    }
}

bool WriteWShadowMapHeader(Core::Vector<std::byte>& out, const WShadowMapHeader& header)
{
    const ShadowBakeKey& key = header.key;
    AppendText(out, "wshadowmap\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "shadow_id %llu\n", header.shadowId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "resolution %u\n", header.resolution);
    AppendTextF(out, "faces %u\n", key.faceCount);
    AppendTextF(out, "eye 0x%08x 0x%08x 0x%08x\n", FloatBits(key.eye[0]), FloatBits(key.eye[1]), FloatBits(key.eye[2]));
    AppendTextF(out, "forward 0x%08x 0x%08x 0x%08x\n", FloatBits(key.forward[0]), FloatBits(key.forward[1]), FloatBits(key.forward[2]));
    AppendTextF(out, "tan_half 0x%08x\n", FloatBits(key.tanHalf));
    AppendTextF(out, "near 0x%08x\n", FloatBits(key.nearPlane));
    AppendTextF(out, "far 0x%08x\n", FloatBits(key.farPlane));
    AppendTextF(out, "data_size %llu\n", header.dataSize);
    AppendTextF(out, "uncompressed_size %llu\n", header.uncompressedSize);
    AppendTextF(out, "compression %u\n", static_cast<uint32_t>(header.compressionType));
    AppendText(out, "end_header\n");
    return true;
}

std::optional<WShadowMapHeader> ReadWShadowMapHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }

    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(map.data, map.size);
    if (!in.GetLine(line, LINE_BUF) || strcmp(line, "wshadowmap") != 0) { return std::nullopt; }

    WShadowMapHeader header{};
    ShadowBakeKey& key = header.key;
    bool bCompressionSeen = false;
    while (in.GetLine(line, LINE_BUF)) {
        const char* lineEnd = line + strlen(line);
        if (strcmp(line, "end_header") == 0) {
            if (!bCompressionSeen) { return std::nullopt; }
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            const auto res = std::from_chars(line + 8, lineEnd, header.major);
            if (res.ptr && *res.ptr == ' ') { std::from_chars(res.ptr + 1, lineEnd, header.minor); }
            if (header.major != SHADOW_MAP_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "shadow_id ", 10) == 0) { std::from_chars(line + 10, lineEnd, header.shadowId); }
        else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, lineEnd, header.contentVersion); }
        else if (strncmp(line, "resolution ", 11) == 0) { std::from_chars(line + 11, lineEnd, header.resolution); }
        else if (strncmp(line, "faces ", 6) == 0) { std::from_chars(line + 6, lineEnd, key.faceCount); }
        else if (strncmp(line, "eye ", 4) == 0) { ParseFloats(line + 4, lineEnd, key.eye, 3); }
        else if (strncmp(line, "forward ", 8) == 0) { ParseFloats(line + 8, lineEnd, key.forward, 3); }
        else if (strncmp(line, "tan_half ", 9) == 0) { ParseFloats(line + 9, lineEnd, &key.tanHalf, 1); }
        else if (strncmp(line, "near ", 5) == 0) { ParseFloats(line + 5, lineEnd, &key.nearPlane, 1); }
        else if (strncmp(line, "far ", 4) == 0) { ParseFloats(line + 4, lineEnd, &key.farPlane, 1); }
        else if (strncmp(line, "data_size ", 10) == 0) { std::from_chars(line + 10, lineEnd, header.dataSize); }
        else if (strncmp(line, "uncompressed_size ", 18) == 0) { std::from_chars(line + 18, lineEnd, header.uncompressedSize); }
        else if (strncmp(line, "compression ", 12) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 12, lineEnd, v);
            header.compressionType = static_cast<CompressionType>(v);
            bCompressionSeen = true;
        }
    }
    return std::nullopt;
}
} // Engine
