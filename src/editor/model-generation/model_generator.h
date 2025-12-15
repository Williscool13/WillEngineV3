//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_GENERATOR_H
#define WILL_ENGINE_MODEL_GENERATOR_H
#include <filesystem>

#include "model_generation_types.h"
#include "fastgltf/types.hpp"

namespace Render
{
class ModelGenerator
{
public:
    static RawGltfModel LoadGltf(const std::filesystem::path& source);

private:
    static VkFilter ExtractFilter(fastgltf::Filter filter);

    static VkSamplerMipmapMode ExtractMipmapMode(fastgltf::Filter filter);

    static MaterialProperties ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial);

    static void LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform);

    static glm::vec4 GenerateBoundingSphere(const std::vector<Vertex>& vertices);
};
} // Render

#endif //WILL_ENGINE_MODEL_GENERATOR_H
