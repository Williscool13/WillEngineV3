//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_FORMAT_H
#define WILL_ENGINE_MODEL_FORMAT_H

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <vector>

#include "model_types.h"
#include "static_model.h"

namespace Engine
{
constexpr char STATIC_MODEL_MAGIC[8] = "WSTCMDL";
constexpr uint32_t STATICMODEL_MAJOR_VERSION = 1;
constexpr uint32_t STATICMODEL_MINOR_VERSION = 2;
constexpr size_t WSTATICMODEL_NAME_LENGTH = 128;

struct WStaticModelHeader
{
    uint64_t modelId{0};
    char name[WSTATICMODEL_NAME_LENGTH]{};

    uint32_t major{STATICMODEL_MAJOR_VERSION};
    uint32_t minor{STATICMODEL_MINOR_VERSION};

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
};

struct WStaticModelInfo
{
    WStaticModelHeader header;
    std::vector<Node> nodes;
    ModelBounds bounds{};
};

bool WriteWStaticModelHeader(std::ostream& out, const WStaticModelHeader& header);

std::optional<WStaticModelHeader> ReadWStaticModelHeader(std::istream& in);

std::optional<WStaticModelInfo> ReadWStaticModelInfo(const std::filesystem::path& path);
} // Engine

#endif //WILL_ENGINE_MODEL_FORMAT_H
