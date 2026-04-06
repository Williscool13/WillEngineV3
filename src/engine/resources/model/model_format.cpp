//
// Created by William on 2026-03-12.
//

#include "model_format.h"

#include <fstream>
#include <istream>
#include <ostream>
#include <string>

#include "engine/serialization/serialization.h"

namespace Engine
{
bool WriteWStaticModelHeader(std::ostream& out, const WStaticModelHeader& header)
{
    out << "wstaticmodel\n";
    out << "version " << header.major << " " << header.minor << "\n";
    out << "id " << header.modelId << "\n";
    out << "name " << header.name << "\n";
    out << "node_count " << header.nodeCount << "\n";
    out << "mesh_node_count " << header.meshNodeCount << "\n";
    out << "vertex_offset " << header.vertexOffset << "\n";
    out << "vertex_count " << header.vertexCount << "\n";
    out << "index_offset " << header.indexOffset << "\n";
    out << "index_count " << header.indexCount << "\n";
    out << "meshlet_vertex_offset " << header.meshletVertexOffset << "\n";
    out << "meshlet_vertex_count " << header.meshletVertexCount << "\n";
    out << "meshlet_triangle_offset " << header.meshletTriangleOffset << "\n";
    out << "meshlet_triangle_count " << header.meshletTriangleCount << "\n";
    out << "meshlet_offset " << header.meshletOffset << "\n";
    out << "meshlet_count " << header.meshletCount << "\n";
    out << "primitive_offset " << header.primitiveOffset << "\n";
    out << "primitive_count " << header.primitiveCount << "\n";
    out << "material_offset " << header.materialOffset << "\n";
    out << "material_count " << header.materialCount << "\n";
    out << "mesh_offset " << header.meshOffset << "\n";
    out << "mesh_count " << header.meshCount << "\n";
    out << "compressed_body_size " << header.compressedBodySize << "\n";
    out << "uncompressed_body_size " << header.uncompressedBodySize << "\n";
    out << "end_header\n";
    return out.good();
}

std::optional<WStaticModelHeader> ReadWStaticModelHeader(std::istream& in)
{
    // todo fewer strings please. and those other std:: are they allocating?
    auto trimCR = [](std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    };

    std::string line;
    if (!std::getline(in, line)) { return std::nullopt; }
    trimCR(line);
    if (line != "wstaticmodel") { return std::nullopt; }

    WStaticModelHeader header{};
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
            if (std::stoul(rest.substr(0, space)) != STATICMODEL_MAJOR_VERSION) return std::nullopt;
        }
        else if (line.starts_with("id ")) { header.modelId = std::stoull(line.substr(3)); }
        else if (line.starts_with("name ")) {
            auto name = line.substr(5);
            auto copyLen = std::min(name.size(), WSTATICMODEL_NAME_LENGTH - 1);
            memcpy(header.name, name.c_str(), copyLen);
            header.name[copyLen] = '\0';
        }
        else if (line.starts_with("node_count ")) { header.nodeCount = std::stoul(line.substr(11)); }
        else if (line.starts_with("mesh_node_count ")) { header.meshNodeCount = std::stoul(line.substr(16)); }
        else if (line.starts_with("vertex_offset ")) { header.vertexOffset = std::stoul(line.substr(14)); }
        else if (line.starts_with("vertex_count ")) { header.vertexCount = std::stoul(line.substr(13)); }
        else if (line.starts_with("index_offset ")) { header.indexOffset = std::stoul(line.substr(13)); }
        else if (line.starts_with("index_count ")) { header.indexCount = std::stoul(line.substr(12)); }
        else if (line.starts_with("meshlet_vertex_offset ")) { header.meshletVertexOffset = std::stoul(line.substr(22)); }
        else if (line.starts_with("meshlet_vertex_count ")) { header.meshletVertexCount = std::stoul(line.substr(21)); }
        else if (line.starts_with("meshlet_triangle_offset ")) { header.meshletTriangleOffset = std::stoul(line.substr(24)); }
        else if (line.starts_with("meshlet_triangle_count ")) { header.meshletTriangleCount = std::stoul(line.substr(23)); }
        else if (line.starts_with("meshlet_offset ")) { header.meshletOffset = std::stoul(line.substr(15)); }
        else if (line.starts_with("meshlet_count ")) { header.meshletCount = std::stoul(line.substr(14)); }
        else if (line.starts_with("primitive_offset ")) { header.primitiveOffset = std::stoul(line.substr(17)); }
        else if (line.starts_with("primitive_count ")) { header.primitiveCount = std::stoul(line.substr(16)); }
        else if (line.starts_with("material_offset ")) { header.materialOffset = std::stoul(line.substr(16)); }
        else if (line.starts_with("material_count ")) { header.materialCount = std::stoul(line.substr(15)); }
        else if (line.starts_with("mesh_offset ")) { header.meshOffset = std::stoul(line.substr(12)); }
        else if (line.starts_with("mesh_count ")) { header.meshCount = std::stoul(line.substr(11)); }
        else if (line.starts_with("compressed_body_size ")) { header.compressedBodySize = std::stoull(line.substr(21)); }
        else if (line.starts_with("uncompressed_body_size ")) { header.uncompressedBodySize = std::stoull(line.substr(23)); }
    }
    return std::nullopt;
}

std::optional<WStaticModelInfo> ReadWStaticModelInfo(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) { return std::nullopt; }

    auto optHeader = ReadWStaticModelHeader(file);
    if (!optHeader) { return std::nullopt; }

    const WStaticModelHeader& header = *optHeader;

    file.seekg(0, std::ios::end);
    const size_t fileSize = static_cast<size_t>(file.tellg());
    const size_t nodeDataStart = header.dataOffset + header.compressedBodySize;
    if (fileSize < nodeDataStart) { return std::nullopt; }

    const size_t nodesSize = fileSize - nodeDataStart;
    std::vector<uint8_t> buf(nodesSize);
    file.seekg(static_cast<std::streamoff>(nodeDataStart));
    file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(nodesSize));
    if (!file) { return std::nullopt; }

    WStaticModelInfo info{};
    info.header = header;
    info.nodes.resize(header.nodeCount);
    const uint8_t* ptr = buf.data();
    for (uint32_t i = 0; i < header.nodeCount; ++i) {
        ReadNode(ptr, info.nodes[i]);
    }

    const size_t bytesConsumed = ptr - buf.data();
    const size_t remaining = nodesSize - bytesConsumed;
    if (remaining >= sizeof(ModelBounds)) {
        memcpy(&info.bounds, ptr, sizeof(ModelBounds));
    }

    return info;
}
} // Engine
