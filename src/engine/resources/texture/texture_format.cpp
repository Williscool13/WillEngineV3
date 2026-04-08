//
// Created by William on 2026-03-09.
//

#include "texture_format.h"

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
    out << "name " << header.name << "\n";
    out << "width " << header.width << "\n";
    out << "height " << header.height << "\n";
    out << "mips " << header.mipCount << "\n";
    out << "data_size " << header.dataSize << "\n";
    out << "uncompressed_size " << header.uncompressedSize << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WTextureHeader> ReadWTextureHeader(std::istream& in)
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
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = static_cast<uint64_t>(in.tellg());
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != TEXTURE_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) {
            std::from_chars(line + 3, line + LINE_BUF, header.textureId);
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
    }
    return std::nullopt;
}

std::optional<WTextureHeader> ReadWTextureHeader(const Core::Path& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    return ReadWTextureHeader(f);
}
} // Engine
