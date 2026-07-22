//
// Created by William on 2026-03-21.
//

#include "prefab_format.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

#include <json/nlohmann/json.hpp>

namespace Engine
{
bool WriteWPrefabHeader(std::ostream& out, const WPrefabHeader& header)
{
    out << "wprefab\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.prefabId << "\n";
    out << "content_version " << header.contentVersion << "\n";
    out << "name " << header.name << "\n";
    out << "component_count " << header.componentCount << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WPrefabHeader> ReadWPrefabHeader(std::istream& in)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wprefab") != 0) { return std::nullopt; }

    WPrefabHeader header{};
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = static_cast<uint64_t>(in.tellg());
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != PREFAB_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + LINE_BUF, header.prefabId); }
        else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.contentVersion); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WPREFAB_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
        else if (strncmp(line, "component_count ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.componentCount); }
    }
    return std::nullopt;
}

std::optional<WPrefabHeader> ReadWPrefabHeader(const Core::Path& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    return ReadWPrefabHeader(f);
}

std::optional<WPrefabData> ReadWPrefab(const char* path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        return std::nullopt;
    }

    auto header = ReadWPrefabHeader(f);
    if (!header) {
        return std::nullopt;
    }

    auto json = nlohmann::json::parse(f, nullptr, false);
    if (json.is_discarded()) {
        return std::nullopt;
    }

    return WPrefabData{.header = *header, .componentJson = std::move(json)};
}
} // Engine
