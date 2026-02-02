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

namespace Editor
{
constexpr uint32_t ASSET_GENERATOR_WORKER_COUNT = 4;
constexpr uint32_t MODEL_GENERATION_JOB_COUNT = 4;
constexpr uint32_t MODEL_GENERATION_STAGING_BUFFER_SIZE = 64 * 1024 * 1024; // 64MB exactly 1x uncompressed 4k image

static constexpr std::array<const char*, ASSET_GENERATOR_WORKER_COUNT> ASSET_GENERATOR_WORKER_NAMES = {
    "AssetGenerator0", "AssetGenerator1", "AssetGenerator2", "AssetGenerator3"
};

struct RawGltfModel
{
    std::string name{};
    bool bSuccessfullyLoaded{false};
    bool bIsSkeletalModel{false};

    std::vector<VkSamplerCreateInfo> samplerInfos{};
    std::vector<Render::AllocatedImage> images{};
    // ktx_transcode_fmt_e
    std::vector<uint32_t> preferredImageFormats{};

    // todo: decouple skinned and non-skinned vertex properties. Make a new vector below this one w/ skinned stuff
    std::vector<SkinnedVertex> vertices{};
    std::vector<uint32_t> meshletVertices{};
    std::vector<uint8_t> meshletTriangles{};
    std::vector<Meshlet> meshlets{};

    std::vector<MeshletPrimitive> primitives{};
    std::vector<MaterialProperties> materials{};

    std::vector<Render::MeshInformation> allMeshes{};
    std::vector<Render::Node> nodes{};

    std::vector<Render::Animation> animations;
    std::vector<glm::mat4> inverseBindMatrices{};
};

} // Render

#endif //WILL_ENGINE_MODEL_GENERATION_TYPES_H