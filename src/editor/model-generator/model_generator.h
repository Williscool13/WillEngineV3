//
// Created by William on 2026-01-02.
//

#ifndef WILL_ENGINE_MODEL_GENERATOR_H
#define WILL_ENGINE_MODEL_GENERATOR_H
#include <filesystem>
#include <glm/glm.hpp>
#include <cstddef>

#include "render/shaders/model_interop.h"

// Forward declarations to avoid including cgltf in header
struct cgltf_data;

namespace Editor
{
struct MeshletBuildResult
{
    std::vector<Meshlet> meshlets;
    std::vector<uint32_t> meshletVertices;
    std::vector<uint8_t> meshletIndices;
};

struct MeshletBufferIndices
{
    size_t meshletViewIdx;
    size_t vertexViewIdx;
    size_t triangleViewIdx;
};
class ModelGenerator
{
public:
    static MeshletBuildResult BuildMeshlets(std::vector<glm::vec3> vertices, std::vector<uint32_t> indices);

    static bool ProcessModelsWithMeshlet(std::filesystem::path input, std::filesystem::path output);

    static MeshletBufferIndices AddMeshletBuffers(cgltf_data* data, const std::vector<Meshlet>& meshlets, const std::vector<uint32_t>& meshletVerts, const std::vector<uint8_t>& meshletTris);
};

} // Editor

#endif //WILL_ENGINE_MODEL_GENERATOR_H
