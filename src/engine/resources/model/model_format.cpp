//
// Created by William on 2026-03-12.
//

#include "model_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/serialization.h"
#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWStaticModelHeader(Core::Vector<std::byte>& out, const WStaticModelHeader& header)
{
    AppendText(out, "wstaticmodel\n");
    AppendTextF(out, "version %u\n", header.version);
    AppendTextF(out, "id %llu\n", header.modelId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "node_count %u\n", header.nodeCount);
    AppendTextF(out, "mesh_node_count %u\n", header.meshNodeCount);
    AppendTextF(out, "vertex_offset %u\n", header.vertexOffset);
    AppendTextF(out, "vertex_count %u\n", header.vertexCount);
    AppendTextF(out, "index_offset %u\n", header.indexOffset);
    AppendTextF(out, "index_count %u\n", header.indexCount);
    AppendTextF(out, "meshlet_vertex_offset %u\n", header.meshletVertexOffset);
    AppendTextF(out, "meshlet_vertex_count %u\n", header.meshletVertexCount);
    AppendTextF(out, "meshlet_triangle_offset %u\n", header.meshletTriangleOffset);
    AppendTextF(out, "meshlet_triangle_count %u\n", header.meshletTriangleCount);
    AppendTextF(out, "meshlet_offset %u\n", header.meshletOffset);
    AppendTextF(out, "meshlet_count %u\n", header.meshletCount);
    AppendTextF(out, "primitive_offset %u\n", header.primitiveOffset);
    AppendTextF(out, "primitive_count %u\n", header.primitiveCount);
    AppendTextF(out, "material_offset %u\n", header.materialOffset);
    AppendTextF(out, "material_count %u\n", header.materialCount);
    AppendTextF(out, "mesh_offset %u\n", header.meshOffset);
    AppendTextF(out, "mesh_count %u\n", header.meshCount);
    AppendTextF(out, "compressed_body_size %llu\n", header.compressedBodySize);
    AppendTextF(out, "uncompressed_body_size %llu\n", header.uncompressedBodySize);
    AppendTextF(out, "compression %u\n", static_cast<uint32_t>(header.compressionType));
    AppendText(out, "end_header\n");
    return true;
}

static std::optional<WStaticModelHeader> ReadWStaticModelHeaderInternal(const void* data, uint64_t size, bool bAnyVersion)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wstaticmodel") != 0) { return std::nullopt; }

    WStaticModelHeader header{};
    bool bCompressionSeen = false;
    while (in.GetLine(line, LINE_BUF)) {
        if (strcmp(line, "end_header") == 0) {
            if (!bCompressionSeen) { return std::nullopt; }
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            std::from_chars(line + 8, line + LINE_BUF, header.version);
            if (!bAnyVersion && header.version != STATICMODEL_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + LINE_BUF, header.modelId); }
        else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.contentVersion); }
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
        else if (strncmp(line, "compression ", 12) == 0) {
            uint32_t v = 0;
            std::from_chars(line + 12, line + LINE_BUF, v);
            header.compressionType = static_cast<CompressionType>(v);
            bCompressionSeen = true;
        }
    }
    return std::nullopt;
}

std::optional<WStaticModelHeader> ReadWStaticModelHeader(const void* data, uint64_t size)
{
    return ReadWStaticModelHeaderInternal(data, size, false);
}

std::optional<WStaticModelHeader> ReadWStaticModelHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWStaticModelHeaderInternal(map.data, map.size, false);
}

std::optional<WStaticModelHeader> ReadWStaticModelHeaderAnyVersion(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWStaticModelHeaderInternal(map.data, map.size, true);
}

std::optional<WStaticModelData> ReadWStaticModelNodes(const Core::Path& path, const WStaticModelHeader& header, Core::TlsfAllocator& allocator)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }

    const uint64_t nodeDataStart = header.dataOffset + header.compressedBodySize;
    if (map.size < nodeDataStart) { return std::nullopt; }
    const size_t nodesSize = map.size - nodeDataStart;

    WStaticModelData modelData{};
    modelData.nodes = Core::HeapArray<Node>(&allocator, Core::AllocTag::Render, header.nodeCount);
    const uint8_t* ptr = map.data + nodeDataStart;
    for (uint32_t i = 0; i < header.nodeCount; ++i) {
        ReadNode(ptr, modelData.nodes[i]);
    }

    const size_t bytesConsumed = ptr - (map.data + nodeDataStart);
    const size_t remaining = nodesSize - bytesConsumed;
    if (remaining >= sizeof(ModelBounds)) {
        memcpy(&modelData.bounds, ptr, sizeof(ModelBounds));
    }

    return modelData;
}
} // Engine
