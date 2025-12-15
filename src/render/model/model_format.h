//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_FORMAT_H
#define WILL_ENGINE_MODEL_FORMAT_H
#include <cstdint>
#include <string>
#include <vector>

#include <volk.h>

#include "model_types.h"
#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
constexpr char MODEL_MAGIC[8] = "WILLGEO";
constexpr uint32_t MODEL_MAJOR_VERSION = 0;
constexpr uint32_t MODEL_MINOR_VERSION = 1;
constexpr uint32_t MODEL_PATCH_VERSION = 1;

struct ModelBinaryHeader
{
    char magic[8];
    uint32_t majorVersion;
    uint32_t minorVersion;
    uint32_t patchVersion;

    // Counts for each array
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
};

struct WillModel
{
    std::string name{};
    bool bSuccessfullyLoaded{false};
    bool bIsSkeletalModel{false};

    std::vector<VkSamplerCreateInfo> samplerInfos{};
    std::vector<Sampler> samplers{};
    std::vector<AllocatedImage> images{};
    std::vector<ImageView> imageViews{};

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> meshletVertices{};
    std::vector<uint8_t> meshletTriangles{};
    std::vector<Meshlet> meshlets{};

    std::vector<MeshletPrimitive> primitives{};
    std::vector<MaterialProperties> materials{};

    std::vector<MeshInformation> allMeshes{};
    std::vector<Node> nodes{};
    std::vector<uint32_t> nodeRemap{};

    std::vector<Animation> animations;
    std::vector<glm::mat4> inverseBindMatrices{};
};
} // Render

#endif //WILL_ENGINE_MODEL_FORMAT_H
