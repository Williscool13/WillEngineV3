//
// Created by William on 2026-04-15.
//

#include "environment_map_format.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

namespace Engine
{
bool WriteWEnvMapHeader(std::ostream& out, const WEnvMapHeader& header)
{
    out << "wenvmap\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.environmentMapId << "\n";
    out << "content_version " << header.contentVersion << "\n";
    out << "name " << header.name << "\n";
    out << "width " << header.width << "\n";
    out << "height " << header.height << "\n";
    out << "mips " << header.mipCount << "\n";
    out << "data_size " << header.dataSize << "\n";
    out << "uncompressed_size " << header.uncompressedSize << "\n";
    out << "compression " << static_cast<uint32_t>(header.compressionType) << "\n";
    out << "end_header\n";
    return out.good();
}

static std::optional<WEnvMapHeader> ReadWEnvMapHeaderInternal(std::istream& in, bool bAnyVersion)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wenvmap") != 0) { return std::nullopt; }

    WEnvMapHeader header{};
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

std::optional<WEnvMapHeader> ReadWEnvMapHeader(std::istream& in)
{
    return ReadWEnvMapHeaderInternal(in, false);
}

std::optional<WEnvMapHeader> ReadWEnvMapHeader(const Core::Path& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    return ReadWEnvMapHeader(f);
}

std::optional<WEnvMapHeader> ReadWEnvMapHeaderAnyVersion(const Core::Path& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    return ReadWEnvMapHeaderInternal(f, true);
}
} // Engine
