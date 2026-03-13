//
// Created by William on 2026-03-13.
//

#include "scene_format.h"

#include <fstream>
#include <istream>
#include <ostream>
#include <string>

namespace Engine
{
bool WriteWSceneHeader(std::ostream& out, const WSceneHeader& header)
{
    out << "wscene\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.sceneId << "\n";
    out << "name " << header.name << "\n";
    out << "entity_count " << header.entityCount << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WSceneHeader> ReadWSceneHeader(std::istream& in)
{
    auto trimCR = [](std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    };

    std::string line;
    if (!std::getline(in, line)) { return std::nullopt; }
    trimCR(line);
    if (line != "wscene") { return std::nullopt; }

    WSceneHeader header{};
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
            if (std::stoul(rest.substr(0, space)) != SCENE_MAJOR_VERSION) return std::nullopt;
        }
        else if (line.starts_with("id ")) { header.sceneId = std::stoull(line.substr(3)); }
        else if (line.starts_with("name ")) {
            auto name = line.substr(5);
            auto copyLen = std::min(name.size(), WSCENE_NAME_LENGTH - 1);
            memcpy(header.name, name.c_str(), copyLen);
            header.name[copyLen] = '\0';
        }
        else if (line.starts_with("entity_count ")) { header.entityCount = std::stoul(line.substr(13)); }
    }
    return std::nullopt;
}

std::optional<WSceneHeader> ReadWSceneHeader(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return ReadWSceneHeader(f);
}
} // Engine
