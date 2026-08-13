//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_FORMAT_H
#define WILL_ENGINE_MODEL_FORMAT_H

#include <optional>

#include "core/containers/vector.h"
#include "model_types.h"
#include "static_model.h"
#include "engine/compression/compression.h"

namespace Engine
{
constexpr char STATIC_MODEL_MAGIC[8] = "WSTCMDL";
constexpr uint32_t STATICMODEL_VERSION = 6;
constexpr size_t WSTATICMODEL_NAME_LENGTH = 128;

struct WStaticModelHeader
{
    uint64_t modelId{0};
    char name[WSTATICMODEL_NAME_LENGTH]{};

    uint32_t version{STATICMODEL_VERSION};

    uint64_t contentVersion{0};

    uint32_t nodeCount{0};
    uint32_t meshNodeCount{0};

    uint32_t vertexOffset{0};
    uint32_t vertexCount{0};
    uint32_t indexOffset{0};
    uint32_t indexCount{0};
    uint32_t meshletVertexOffset{0};
    uint32_t meshletVertexCount{0};
    uint32_t meshletTriangleOffset{0};
    uint32_t meshletTriangleCount{0};
    uint32_t meshletOffset{0};
    uint32_t meshletCount{0};
    uint32_t primitiveOffset{0};
    uint32_t primitiveCount{0};
    uint32_t materialOffset{0};
    uint32_t materialCount{0};
    uint32_t meshOffset{0};
    uint32_t meshCount{0};
    /**
     * bytes
     */
    uint64_t compressedBodySize{0};
    /**
     * bytes
     */
    uint64_t uncompressedBodySize{0};
    uint64_t dataOffset{0};
    CompressionType compressionType{DEFAULT_STATIC_MODEL_COMPRESSION};
};

struct WStaticModelData
{
    Core::HeapArray<Node> nodes;
    ModelBounds bounds{};
};

bool WriteWStaticModelHeader(Core::Vector<std::byte>& out, const WStaticModelHeader& header);

std::optional<WStaticModelHeader> ReadWStaticModelHeader(const void* data, uint64_t size);
std::optional<WStaticModelHeader> ReadWStaticModelHeader(const Core::Path& path);

/** Reads header without rejecting on version mismatch. For use by the asset generator to detect stale files. */
std::optional<WStaticModelHeader> ReadWStaticModelHeaderAnyVersion(const Core::Path& path);

/**
 * Reads nodes and bounds data.
 * WStaticModelData::nodes will be allocated using the `allocator` (so it can be std::move-d by the caller.
 * @param path
 * @param header
 * @param allocator
 * @return
 */
std::optional<WStaticModelData> ReadWStaticModelNodes(const Core::Path& path, const WStaticModelHeader& header, Core::TlsfAllocator& allocator);
} // Engine

#endif //WILL_ENGINE_MODEL_FORMAT_H
