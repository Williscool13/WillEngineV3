//
// Created by William on 2026-07-22.
//

#include "probe_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
static const char* ParseFloats(const char* s, const char* end, float* out, int32_t count)
{
    for (int32_t i = 0; i < count; ++i) {
        while (s < end && (*s == ' ' || *s == '\t')) { ++s; }
        if (end - s > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; }
        uint32_t bits = 0;
        auto result = std::from_chars(s, end, bits, 16);
        if (result.ec != std::errc{}) { return nullptr; }
        out[i] = std::bit_cast<float>(bits);
        s = result.ptr;
    }
    return s;
}

bool WriteWProbeHeader(Core::Vector<std::byte>& out, const WProbeHeader& header)
{
    AppendText(out, "wprobe\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "probe_id %llu\n", header.probeId);
    AppendTextF(out, "id %llu\n", header.environmentMapId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "width %u\n", header.width);
    AppendTextF(out, "height %u\n", header.height);
    AppendTextF(out, "mips %u\n", header.mipCount);
    AppendTextF(out, "data_size %llu\n", header.dataSize);
    AppendTextF(out, "uncompressed_size %llu\n", header.uncompressedSize);
    AppendTextF(out, "compression %u\n", static_cast<uint32_t>(header.compressionType));
    AppendTextF(out, "resolution %u\n", header.resolution);
    AppendTextF(out, "translation 0x%08x 0x%08x 0x%08x\n", FloatBits(header.snapshot.translation[0]), FloatBits(header.snapshot.translation[1]), FloatBits(header.snapshot.translation[2]));
    AppendTextF(out, "rotation 0x%08x 0x%08x 0x%08x 0x%08x\n", FloatBits(header.snapshot.rotation[0]), FloatBits(header.snapshot.rotation[1]), FloatBits(header.snapshot.rotation[2]), FloatBits(header.snapshot.rotation[3]));
    AppendTextF(out, "scale 0x%08x 0x%08x 0x%08x\n", FloatBits(header.snapshot.scale[0]), FloatBits(header.snapshot.scale[1]), FloatBits(header.snapshot.scale[2]));
    AppendTextF(out, "capture_offset 0x%08x 0x%08x 0x%08x\n", FloatBits(header.snapshot.captureOffset[0]), FloatBits(header.snapshot.captureOffset[1]), FloatBits(header.snapshot.captureOffset[2]));
    AppendText(out, "end_header\n");
    return true;
}

static std::optional<WProbeHeader> ReadWProbeHeaderInternal(const void* data, uint64_t size, bool bAnyVersion)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wprobe") != 0) { return std::nullopt; }

    WProbeHeader header{};
    bool bCompressionSeen = false;
    while (in.GetLine(line, LINE_BUF)) {
        const char* lineEnd = line + strlen(line);
        if (strcmp(line, "end_header") == 0) {
            if (!bCompressionSeen) { return std::nullopt; }
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            auto res = std::from_chars(line + 8, lineEnd, header.major);
            if (res.ptr && *res.ptr == ' ') { std::from_chars(res.ptr + 1, lineEnd, header.minor); }
            if (!bAnyVersion && header.major != PROBE_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "probe_id ", 9) == 0) { std::from_chars(line + 9, lineEnd, header.probeId); }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, lineEnd, header.environmentMapId); }
        else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, lineEnd, header.contentVersion); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WENVMAP_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
        else if (strncmp(line, "width ", 6) == 0) { std::from_chars(line + 6, lineEnd, header.width); }
        else if (strncmp(line, "height ", 7) == 0) { std::from_chars(line + 7, lineEnd, header.height); }
        else if (strncmp(line, "mips ", 5) == 0) { std::from_chars(line + 5, lineEnd, header.mipCount); }
        else if (strncmp(line, "data_size ", 10) == 0) { std::from_chars(line + 10, lineEnd, header.dataSize); }
        else if (strncmp(line, "uncompressed_size ", 18) == 0) { std::from_chars(line + 18, lineEnd, header.uncompressedSize); }
        else if (strncmp(line, "compression ", 12) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 12, lineEnd, v);
            header.compressionType = static_cast<CompressionType>(v);
            bCompressionSeen = true;
        }
        else if (strncmp(line, "resolution ", 11) == 0) { std::from_chars(line + 11, lineEnd, header.resolution); }
        else if (strncmp(line, "translation ", 12) == 0) { ParseFloats(line + 12, lineEnd, header.snapshot.translation, 3); }
        else if (strncmp(line, "rotation ", 9) == 0) { ParseFloats(line + 9, lineEnd, header.snapshot.rotation, 4); }
        else if (strncmp(line, "scale ", 6) == 0) { ParseFloats(line + 6, lineEnd, header.snapshot.scale, 3); }
        else if (strncmp(line, "capture_offset ", 15) == 0) { ParseFloats(line + 15, lineEnd, header.snapshot.captureOffset, 3); }
    }
    return std::nullopt;
}

std::optional<WProbeHeader> ReadWProbeHeader(const void* data, uint64_t size)
{
    return ReadWProbeHeaderInternal(data, size, false);
}

std::optional<WProbeHeader> ReadWProbeHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWProbeHeaderInternal(map.data, map.size, false);
}

std::optional<WProbeHeader> ReadWProbeHeaderAnyVersion(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWProbeHeaderInternal(map.data, map.size, true);
}
} // Engine
