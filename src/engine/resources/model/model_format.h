//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_FORMAT_H
#define WILL_ENGINE_MODEL_FORMAT_H

#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"

namespace Engine
{
constexpr char STATIC_MODEL_MAGIC[8] = "WSCMESH";
constexpr uint32_t MODEL_MAJOR_VERSION = 0;
constexpr uint32_t MODEL_MINOR_VERSION = 8;
constexpr uint32_t MODEL_PATCH_VERSION = 0;

struct ModelBinaryHeader
{
    uint32_t vertexCount;
    uint32_t meshletVertexCount;
    uint32_t meshletTriangleCount;
    uint32_t meshletCount;
    uint32_t primitiveCount;
    uint32_t materialCount;
    uint32_t meshCount;
    uint32_t animationCount;
    uint32_t inverseBindMatrixCount;
};

constexpr size_t MAX_FILENAME_LENGTH = 128;

enum class CompressionType : uint32_t
{
    None = 0,
    Zlib = 1,
    LZ4  = 2,
};

struct FileEntry
{
    char filename[MAX_FILENAME_LENGTH];
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t uncompressedSize;
    CompressionType compressionType;
};

struct ModelMetadata
{
    uint32_t nodeCount{0};
    uint32_t meshNodeCount{0};
};

struct StaticModelHeader
{
    char magic[8];
    uint32_t majorVersion;
    uint32_t minorVersion;
    uint32_t patchVersion;
    uint32_t numFiles;
    uint64_t fileTableOffset;
    ModelMetadata metadata;
};
} // Render

#endif //WILL_ENGINE_MODEL_FORMAT_H
