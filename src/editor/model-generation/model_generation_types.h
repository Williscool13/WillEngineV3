//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_GENERATION_TYPES_H
#define WILL_ENGINE_MODEL_GENERATION_TYPES_H
#include <string>
#include <vector>

#include <volk.h>

#include "render/model/model_types.h"
#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct RawGltfModel
{
    std::string name{};
    bool bSuccessfullyLoaded{false};
    bool bIsSkeletalModel{false};

    std::vector<VkSamplerCreateInfo> samplerInfos{};
    std::vector<AllocatedImage> images{};

    std::vector<SkinnedVertex> vertices{};
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

#endif //WILL_ENGINE_MODEL_GENERATION_TYPES_H