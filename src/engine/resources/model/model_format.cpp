//
// Created by William on 2026-03-12.
//

#include "model_format.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

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
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];

    auto trimCR = [](char* s) {
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\r') { s[len - 1] = '\0'; }
    };

    if (!in.getline(line, LINE_BUF)) { return std::nullopt; }
    trimCR(line);
    if (strcmp(line, "wstaticmodel") != 0) { return std::nullopt; }

    WStaticModelHeader header{};
    while (in.getline(line, LINE_BUF)) {
        trimCR(line);
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = static_cast<uint64_t>(in.tellg());
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != STATICMODEL_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + LINE_BUF, header.modelId); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WSTATICMODEL_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
        else if (strncmp(line, "node_count ", 11) == 0) { std::from_chars(line + 11, line + LINE_BUF, header.nodeCount); }
        else if (strncmp(line, "mesh_node_count ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.meshNodeCount); }
        else if (strncmp(line, "vertex_offset ", 14) == 0) { std::from_chars(line + 14, line + LINE_BUF, header.vertexOffset); }
        else if (strncmp(line, "vertex_count ", 13) == 0) { std::from_chars(line + 13, line + LINE_BUF, header.vertexCount); }
        else if (strncmp(line, "index_offset ", 13) == 0) { std::from_chars(line + 13, line + LINE_BUF, header.indexOffset); }
        else if (strncmp(line, "index_count ", 12) == 0) { std::from_chars(line + 12, line + LINE_BUF, header.indexCount); }
        else if (strncmp(line, "meshlet_vertex_offset ", 22) == 0) { std::from_chars(line + 22, line + LINE_BUF, header.meshletVertexOffset); }
        else if (strncmp(line, "meshlet_vertex_count ", 21) == 0) { std::from_chars(line + 21, line + LINE_BUF, header.meshletVertexCount); }
        else if (strncmp(line, "meshlet_triangle_offset ", 24) == 0) { std::from_chars(line + 24, line + LINE_BUF, header.meshletTriangleOffset); }
        else if (strncmp(line, "meshlet_triangle_count ", 23) == 0) { std::from_chars(line + 23, line + LINE_BUF, header.meshletTriangleCount); }
        else if (strncmp(line, "meshlet_offset ", 15) == 0) { std::from_chars(line + 15, line + LINE_BUF, header.meshletOffset); }
        else if (strncmp(line, "meshlet_count ", 14) == 0) { std::from_chars(line + 14, line + LINE_BUF, header.meshletCount); }
        else if (strncmp(line, "primitive_offset ", 17) == 0) { std::from_chars(line + 17, line + LINE_BUF, header.primitiveOffset); }
        else if (strncmp(line, "primitive_count ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.primitiveCount); }
        else if (strncmp(line, "material_offset ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.materialOffset); }
        else if (strncmp(line, "material_count ", 15) == 0) { std::from_chars(line + 15, line + LINE_BUF, header.materialCount); }
        else if (strncmp(line, "mesh_offset ", 12) == 0) { std::from_chars(line + 12, line + LINE_BUF, header.meshOffset); }
        else if (strncmp(line, "mesh_count ", 11) == 0) { std::from_chars(line + 11, line + LINE_BUF, header.meshCount); }
        else if (strncmp(line, "compressed_body_size ", 21) == 0) { std::from_chars(line + 21, line + LINE_BUF, header.compressedBodySize); }
        else if (strncmp(line, "uncompressed_body_size ", 23) == 0) { std::from_chars(line + 23, line + LINE_BUF, header.uncompressedBodySize); }
    }
    return std::nullopt;
}

std::optional<WStaticModelHeader> ReadWStaticModelHeader(const Core::Path& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) { return std::nullopt; }
    return ReadWStaticModelHeader(file);
}

std::optional<WStaticModelData> ReadWStaticModelNodes(const Core::Path& path, const WStaticModelHeader& header, Core::TlsfAllocator& allocator, Core::TlsfAllocator& scratchAllocator)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) { return std::nullopt; }

    file.seekg(0, std::ios::end);
    const size_t fileSize = file.tellg();
    const size_t nodeDataStart = header.dataOffset + header.compressedBodySize;
    if (fileSize < nodeDataStart) { return std::nullopt; }

    const size_t nodesSize = fileSize - nodeDataStart;

    auto buf = Core::HeapArray<uint8_t>(&scratchAllocator, Core::AllocTag::AssetManager, nodesSize);
    file.seekg(static_cast<std::streamoff>(nodeDataStart));
    file.read(reinterpret_cast<char*>(buf.Data()), static_cast<std::streamsize>(nodesSize));
    if (!file) { return std::nullopt; }

    WStaticModelData modelData{};
    modelData.nodes = Core::HeapArray<Node>(&allocator, Core::AllocTag::Render, header.nodeCount);
    const uint8_t* ptr = buf.Data();
    for (uint32_t i = 0; i < header.nodeCount; ++i) {
        ReadNode(ptr, modelData.nodes[i]);
    }

    const size_t bytesConsumed = ptr - buf.Data();
    const size_t remaining = nodesSize - bytesConsumed;
    if (remaining >= sizeof(ModelBounds)) {
        memcpy(&modelData.bounds, ptr, sizeof(ModelBounds));
    }

    return modelData;
}
} // Engine
