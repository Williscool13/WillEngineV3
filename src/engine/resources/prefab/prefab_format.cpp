//
// Created by William on 2026-03-21.
//

#include "prefab_format.h"

#include <fstream>
#include <istream>
#include <ostream>
#include <string>

#include <json/nlohmann/json.hpp>

namespace Engine
{
bool WriteWPrefabHeader(std::ostream& out, const WPrefabHeader& header)
{
    out << "wprefab\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.prefabId << "\n";
    out << "name " << header.name << "\n";
    out << "component_count " << header.componentCount << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WPrefabHeader> ReadWPrefabHeader(std::istream& in)
{
    auto trimCR = [](std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    };

    std::string line;
    if (!std::getline(in, line)) { return std::nullopt; }
    trimCR(line);
    if (line != "wprefab") { return std::nullopt; }

    WPrefabHeader header{};
    while (std::getline(in, line)) {
        trimCR(line);
        if (line == "end_header") {
            header.dataOffset = static_cast<uint64_t>(in.tellg());
            return header;
        }
        if (line.starts_with("version ")) {
            const auto rest = line.substr(8);
            const auto space = rest.find(' ');
            if (space == std::string::npos) return std::nullopt;
            if (std::stoul(rest.substr(0, space)) != PREFAB_MAJOR_VERSION) return std::nullopt;
        }
        else if (line.starts_with("id ")) { header.prefabId = std::stoull(line.substr(3)); }
        else if (line.starts_with("name ")) {
            auto name = line.substr(5);
            auto copyLen = std::min(name.size(), WPREFAB_NAME_LENGTH - 1);
            memcpy(header.name, name.c_str(), copyLen);
            header.name[copyLen] = '\0';
        }
        else if (line.starts_with("component_count ")) { header.componentCount = std::stoul(line.substr(16)); }
    }
    return std::nullopt;
}

std::optional<WPrefabHeader> ReadWPrefabHeader(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return ReadWPrefabHeader(f);
}

std::optional<WPrefabData> ReadWPrefab(const std::filesystem::path& path)
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
