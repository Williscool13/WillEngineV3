//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_FORMAT_H
#define WILL_ENGINE_MODEL_FORMAT_H

#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
constexpr char WILL_MODEL_MAGIC[8] = "WILLMDL";
constexpr uint32_t MODEL_MAJOR_VERSION = 0;
constexpr uint32_t MODEL_MINOR_VERSION = 1;
constexpr uint32_t MODEL_PATCH_VERSION = 4;

struct ModelBinaryHeader
{
    uint32_t vertexCount;
    uint32_t meshletVertexCount;
    uint32_t meshletTriangleCount;
    uint32_t meshletCount;
    uint32_t primitiveCount;
    uint32_t materialCount;
    uint32_t meshCount;
    uint32_t nodeCount;
    uint32_t nodeRemapCount;
    uint32_t animationCount;
    uint32_t inverseBindMatrixCount;
    uint32_t samplerCount;
    uint32_t textureCount;
    uint32_t bIsSkeletalModel;
};

constexpr size_t MAX_FILENAME_LENGTH = 128;

struct FileEntry
{
    char filename[MAX_FILENAME_LENGTH];
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t uncompressedSize;
    uint32_t compressionType; // 0 = none, 1 = zlib
};

struct WillModelHeader
{
    char magic[8];
    uint32_t majorVersion;
    uint32_t minorVersion;
    uint32_t patchVersion;
    uint32_t numFiles;
    uint64_t fileTableOffset;
};
} // Render

#endif //WILL_ENGINE_MODEL_FORMAT_H
