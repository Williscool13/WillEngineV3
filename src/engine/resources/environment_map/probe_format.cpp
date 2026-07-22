//
// Created by William on 2026-07-22.
//

#include "probe_format.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

namespace Engine
{
static const char* ParseFloats(const char* s, const char* end, float* out, int32_t count)
{
    for (int32_t i = 0; i < count; ++i) {
        while (s < end && (*s == ' ' || *s == '\t')) { ++s; }
        auto result = std::from_chars(s, end, out[i]);
        if (result.ec != std::errc{}) { return nullptr; }
        s = result.ptr;
    }
    return s;
}

bool WriteWProbeHeader(std::ostream& out, const WProbeHeader& header)
{
    out.precision(9);
    out << "wprobe\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "probe_id " << header.probeId << "\n";
    out << "id " << header.environmentMapId << "\n";
    out << "content_version " << header.contentVersion << "\n";
    out << "name " << header.name << "\n";
    out << "width " << header.width << "\n";
    out << "height " << header.height << "\n";
    out << "mips " << header.mipCount << "\n";
    out << "data_size " << header.dataSize << "\n";
    out << "uncompressed_size " << header.uncompressedSize << "\n";
    out << "compression " << static_cast<uint32_t>(header.compressionType) << "\n";
    out << "resolution " << header.resolution << "\n";
    out << "translation " << header.snapshot.translation[0] << " " << header.snapshot.translation[1] << " " << header.snapshot.translation[2] << "\n";
    out << "rotation " << header.snapshot.rotation[0] << " " << header.snapshot.rotation[1] << " " << header.snapshot.rotation[2] << " " << header.snapshot.rotation[3] << "\n";
    out << "scale " << header.snapshot.scale[0] << " " << header.snapshot.scale[1] << " " << header.snapshot.scale[2] << "\n";
    out << "capture_offset " << header.snapshot.captureOffset[0] << " " << header.snapshot.captureOffset[1] << " " << header.snapshot.captureOffset[2] << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WProbeHeader> ReadWProbeHeader(std::istream& in)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wprobe") != 0) { return std::nullopt; }

    WProbeHeader header{};
    bool bCompressionSeen = false;
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        const char* lineEnd = line + strlen(line);
        if (strcmp(line, "end_header") == 0) {
            if (!bCompressionSeen) { return std::nullopt; }
            header.dataOffset = static_cast<uint64_t>(in.tellg());
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, lineEnd, major);
            if (major != PROBE_MAJOR_VERSION) { return std::nullopt; }
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

std::optional<WProbeHeader> ReadWProbeHeader(const Core::Path& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    return ReadWProbeHeader(f);
}
} // Engine
