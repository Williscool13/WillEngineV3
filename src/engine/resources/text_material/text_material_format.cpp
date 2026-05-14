//
// Created by William on 2026-05-14.
//

#include "text_material_format.h"

#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

namespace Engine
{
bool WriteWTextMaterialHeader(std::ostream& out, const WTextMaterialHeader& header)
{
    out << "wstextmat\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.textMaterialId << "\n";
    out << "name " << header.name << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(std::istream& in)
{
    constexpr size_t LINE_BUF = 512;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wstextmat") != 0) { return std::nullopt; }

    WTextMaterialHeader header{};
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        const char* end = line + strlen(line);
        if (strcmp(line, "end_header") == 0) { return header; }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, end, major);
            if (major != TEXT_MATERIAL_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, end, header.textMaterialId); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WTEXT_MATERIAL_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
    }
    return std::nullopt;
}

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(const Core::Path& path)
{
    std::ifstream file(path.c_str());
    return ReadWTextMaterialHeader(file);
}
} // Engine
