//
// Created by William on 2026-03-09.
//

#include "texture_format.h"

#include <fstream>
#include <istream>
#include <ostream>
#include <string>

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
    out << "end_header\n";
    return out.good();
}

std::optional<WTextureHeader> ReadWTextureHeader(std::istream& in)
{
    auto trimCR = [](std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    };

    std::string line;
    if (!std::getline(in, line)) { return std::nullopt; }
    trimCR(line);
    if (line != "wtexture") { return std::nullopt; }

    WTextureHeader header{};
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
            if (std::stoul(rest.substr(0, space)) != TEXTURE_MAJOR_VERSION) return std::nullopt;
        }
        else if (line.starts_with("id ")) {
            header.textureId = std::stoull(line.substr(3));
        }
        else if (line.starts_with("name ")) {
            auto name = line.substr(5);
            auto copyLen = std::min(name.size(), WTEXTURE_NAME_LENGTH - 1);
            memcpy(header.name, name.c_str(), copyLen);
            header.name[copyLen] = '\0';
        }
        else if (line.starts_with("width ")) { header.width = std::stoul(line.substr(6)); }
        else if (line.starts_with("height ")) { header.height = std::stoul(line.substr(7)); }
        else if (line.starts_with("mips ")) { header.mipCount = std::stoul(line.substr(5)); }
        else if (line.starts_with("data_size ")) { header.dataSize = std::stoull(line.substr(10)); }
    }
    return std::nullopt;
}

std::optional<WTextureHeader> ReadWTextureHeader(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return ReadWTextureHeader(f);
}
} // Engine
