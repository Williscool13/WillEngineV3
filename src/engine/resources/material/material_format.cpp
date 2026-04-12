//
// Created by William on 2026-03-13.
//

#include "material_format.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

namespace Engine
{
bool WriteWMaterialHeader(std::ostream& out, const WMaterialHeader& header)
{
    out << "wmaterial\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.materialId << "\n";
    out << "name " << header.name << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WMaterialHeader> ReadWMaterialHeader(std::istream& in)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wmaterial") != 0) { return std::nullopt; }

    WMaterialHeader header{};
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = static_cast<uint64_t>(in.tellg());
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != MATERIAL_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + LINE_BUF, header.materialId); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WMATERIAL_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
    }
    return std::nullopt;
}

std::optional<WMaterialHeader> ReadWMaterialHeader(const Core::Path& path)
{
    std::ifstream file(path.c_str());
    return ReadWMaterialHeader(file);
}
} // Engine
