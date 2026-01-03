//
// Created by William on 2026-01-02.
//

#ifndef WILL_ENGINE_MODEL_GENERATOR_H
#define WILL_ENGINE_MODEL_GENERATOR_H
#include <complex.h>
#include <complex.h>
#include <filesystem>
#include <glm/glm.hpp>
#include <cstddef>

#include "tiny_gltf.h"
#include "editor/asset-generation/asset_generation_types.h"
#include "editor/asset-generation/asset_generation_types.h"
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
    static void BuildMeshlets(const std::vector<glm::vec3>& vertices,
                              const std::vector<uint32_t>& indices,
                              std::vector<Meshlet>& outMeshlets,
                              std::vector<uint32_t>& outMeshletVertices,
                              std::vector<uint8_t>& outMeshletTriangles);

    static bool ProcessModelsWithMeshlet(const std::filesystem::path& input, const std::filesystem::path& output);

    static bool StubLoadImageData(tinygltf::Image* image,
                       const int image_idx,
                       std::string* err,
                       std::string* warn,
                       int req_width,
                       int req_height,
                       const unsigned char* bytes,
                       int size,
                       void* user_data)
    {
        return true;
    }

    static bool StubWriteImageData(const std::string* basepath,
                        const std::string* filename,
                        const tinygltf::Image* image,
                        bool embedImages,
                        const tinygltf::FsCallbacks* fs_cb,
                        const tinygltf::URICallbacks* uri_cb,
                        std::string* out_uri,
                        void* user_data)
    {
        if (out_uri) {
            *out_uri = *filename;
        }
        return true;
    }

};
} // Editor

#endif //WILL_ENGINE_MODEL_GENERATOR_H
