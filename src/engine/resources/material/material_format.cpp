//
// Created by William on 2026-03-13.
//

#include "material_format.h"

#include <fstream>
#include <istream>
#include <ostream>
#include <string>

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
    auto trimCR = [](std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    };

    std::string line;
    if (!std::getline(in, line)) { return std::nullopt; }
    trimCR(line);
    if (line != "wmaterial") { return std::nullopt; }

    WMaterialHeader header{};
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
            if (std::stoul(rest.substr(0, space)) != MATERIAL_MAJOR_VERSION) return std::nullopt;
        }
        else if (line.starts_with("id ")) { header.materialId = std::stoull(line.substr(3)); }
        else if (line.starts_with("name ")) {
            auto name = line.substr(5);
            auto copyLen = std::min(name.size(), WMATERIAL_NAME_LENGTH - 1);
            memcpy(header.name, name.c_str(), copyLen);
            header.name[copyLen] = '\0';
        }
    }
    return std::nullopt;
}

std::optional<WMaterialHeader> ReadWMaterialHeader(const std::filesystem::path& path)
{
    std::ifstream f(path);
    return ReadWMaterialHeader(f);
}
} // Engine
