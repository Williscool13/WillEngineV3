// Created by William on 2026-01-02.
//

#include "model_generator.h"

#include <glm/glm.hpp>
#include <meshoptimizer.h>
#include <tracy/Tracy.hpp>
#include <json/nlohmann/json.hpp>

#include "render/shaders/constants_interop.h"
#include "render/shaders/model_interop.h"
#include "spdlog/spdlog.h"
#include "tinygltf/tiny_gltf.h"


namespace Editor
{
void ModelGenerator::BuildMeshlets(const std::vector<glm::vec3>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   std::vector<Meshlet>& outMeshlets,
                                   std::vector<uint32_t>& outMeshletVertices,
                                   std::vector<uint8_t>& outMeshletTriangles)
{
    ZoneScopedN("BuildMeshlets");
    size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES);
    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);

    outMeshletVertices.resize(indices.size());
    outMeshletTriangles.resize(indices.size());


    // meshopt_optimizeVertexCache(indices, ...);
    // meshopt_optimizeVertexFetch(vertices, indices);

    // Building
    {
        ZoneScopedN("Building");
        std::vector<uint32_t> primitiveVertexPositions;
        meshlets.resize(meshopt_buildMeshlets(&meshlets[0], &outMeshletVertices[0], &outMeshletTriangles[0],
                                              indices.data(), indices.size(),
                                              reinterpret_cast<const float*>(vertices.data()), vertices.size(), sizeof(glm::vec3),
                                              MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES, 0.f));
    }

    // Optimize each meshlet's micro index buffer/vertex layout individually
    {
        ZoneScopedN("Optimize");
        for (auto& meshlet : meshlets) {
            meshopt_optimizeMeshlet(&outMeshletVertices[meshlet.vertex_offset], &outMeshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
        }
    }

    // Trim the meshlet data to minimize waste for meshletVertices/meshletTriangles
    const meshopt_Meshlet& last = meshlets.back();
    outMeshletVertices.resize(last.vertex_offset + last.vertex_count);
    outMeshletTriangles.resize(last.triangle_offset + last.triangle_count * 3);

    outMeshlets.reserve(meshlets.size());
    // Compute Meshlet Bounds
    {
        ZoneScopedN("ComputeBounds");
        for (meshopt_Meshlet& meshlet : meshlets) {
            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &outMeshletVertices[meshlet.vertex_offset],
                &outMeshletTriangles[meshlet.triangle_offset],
                meshlet.triangle_count,
                reinterpret_cast<const float*>(vertices.data()),
                vertices.size(),
                sizeof(glm::vec3)
            );

            outMeshlets.push_back({
                .meshletBoundingSphere = glm::vec4(
                    bounds.center[0], bounds.center[1], bounds.center[2],
                    bounds.radius
                ),
                .coneApex = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]),
                .coneCutoff = bounds.cone_cutoff,

                .coneAxis = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]),
                .vertexOffset = 0,

                .meshletVertexOffset = meshlet.vertex_offset,
                .meshletTriangleOffset = meshlet.triangle_offset,
                .meshletVerticesCount = meshlet.vertex_count,
                .meshletTriangleCount = meshlet.triangle_count,
            });
        }
    }
}

bool ModelGenerator::ProcessModelsWithMeshlet(const std::filesystem::path& input, const std::filesystem::path& output)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(StubLoadImageData, nullptr);
    loader.SetImageWriter(StubWriteImageData, nullptr);
    std::string err, warn;


    bool ret;
    if (input.extension() == ".gltf") {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, input.string());
    }
    else {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, input.string());
    }

    if (!ret) {
        return false;
    }


    std::vector<uint32_t> meshletVertexIndirectionBuffer;
    std::vector<uint8_t> meshletTriangleBuffer;
    std::vector<Meshlet> meshletBuffer;

    std::vector<uint32_t> tempMeshletVertexIndirectionBuffer;
    std::vector<uint8_t> tempMeshletTriangleBuffer;
    std::vector<Meshlet> tempMeshletBuffer;

    uint32_t vertexOffset{0};
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    for (auto& mesh : model.meshes) {
        for (auto& primitive : mesh.primitives) {
            // Get position data
            const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
            const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
            const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

            positions.clear();
            positions.resize(posAccessor.count);

            auto posData = reinterpret_cast<const float*>(posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset);

            for (size_t i = 0; i < posAccessor.count; ++i) {
                positions[i] = glm::vec3(posData[i * 3], posData[i * 3 + 1], posData[i * 3 + 2]);
            }

            const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
            const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

            indices.clear();
            indices.resize(indexAccessor.count);

            const uint8_t* indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;

            for (size_t i = 0; i < indexAccessor.count; ++i) {
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    indices[i] = reinterpret_cast<const uint16_t*>(indexData)[i];
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    indices[i] = reinterpret_cast<const uint32_t*>(indexData)[i];
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    indices[i] = indexData[i];
                }
            }

            BuildMeshlets(positions, indices, tempMeshletBuffer, tempMeshletVertexIndirectionBuffer, tempMeshletTriangleBuffer);

            uint32_t vertexIndirectionOffset = meshletVertexIndirectionBuffer.size();
            uint32_t triangleOffset = meshletTriangleBuffer.size();
            uint32_t meshletOffset = meshletBuffer.size();

            uint32_t vertexIndirectionCount = tempMeshletVertexIndirectionBuffer.size();
            uint32_t triangleCount = tempMeshletTriangleBuffer.size();
            uint32_t meshletCount = tempMeshletBuffer.size();

            for (Meshlet& meshlet : tempMeshletBuffer) {
                meshlet.vertexOffset += vertexOffset;
                meshlet.meshletTriangleOffset += meshletTriangleBuffer.size();
                meshlet.meshletVertexOffset += meshletVertexIndirectionBuffer.size();
            }
            vertexOffset += positions.size();

            meshletVertexIndirectionBuffer.insert(meshletVertexIndirectionBuffer.end(), tempMeshletVertexIndirectionBuffer.begin(), tempMeshletVertexIndirectionBuffer.end());
            meshletTriangleBuffer.insert(meshletTriangleBuffer.end(), tempMeshletTriangleBuffer.begin(), tempMeshletTriangleBuffer.end());
            meshletBuffer.insert(meshletBuffer.end(), tempMeshletBuffer.begin(), tempMeshletBuffer.end());

            tinygltf::Value::Object extras;
            extras["meshletOffset"] = tinygltf::Value(static_cast<int>(meshletOffset));
            extras["meshletCount"] = tinygltf::Value(static_cast<int>(meshletCount));
            extras["vertexIndirectionOffset"] = tinygltf::Value(static_cast<int>(vertexIndirectionOffset));
            extras["vertexIndirectionCount"] = tinygltf::Value(static_cast<int>(vertexIndirectionCount));
            extras["triangleOffset"] = tinygltf::Value(static_cast<int>(triangleOffset));
            extras["triangleCount"] = tinygltf::Value(static_cast<int>(triangleCount));

            primitive.extras = tinygltf::Value(extras);

            tempMeshletBuffer.clear();
            tempMeshletVertexIndirectionBuffer.clear();
            tempMeshletTriangleBuffer.clear();
        }
    }


    std::vector<uint8_t> meshletData;

    size_t meshletBufferOffset = meshletData.size();
    meshletData.insert(meshletData.end(),
                       reinterpret_cast<uint8_t*>(meshletBuffer.data()),
                       reinterpret_cast<uint8_t*>(meshletBuffer.data() + meshletBuffer.size()));

    size_t vertexIndirectionBufferOffset = meshletData.size();
    meshletData.insert(meshletData.end(),
                       reinterpret_cast<uint8_t*>(meshletVertexIndirectionBuffer.data()),
                       reinterpret_cast<uint8_t*>(meshletVertexIndirectionBuffer.data() + meshletVertexIndirectionBuffer.size()));

    size_t triangleBufferOffset = meshletData.size();
    meshletData.insert(meshletData.end(),
                       meshletTriangleBuffer.begin(),
                       meshletTriangleBuffer.end());

    tinygltf::Buffer buffer;
    buffer.data = std::move(meshletData);
    auto bufferIndex = static_cast<int32_t>(model.buffers.size());
    model.buffers.push_back(buffer);

    tinygltf::BufferView meshletView;
    meshletView.buffer = bufferIndex;
    meshletView.byteOffset = meshletBufferOffset;
    meshletView.byteLength = meshletBuffer.size() * sizeof(Meshlet);
    auto meshletViewIndex = static_cast<int32_t>(model.bufferViews.size());
    model.bufferViews.push_back(meshletView);

    tinygltf::BufferView vertexIndirectionView;
    vertexIndirectionView.buffer = bufferIndex;
    vertexIndirectionView.byteOffset = vertexIndirectionBufferOffset;
    vertexIndirectionView.byteLength = meshletVertexIndirectionBuffer.size() * sizeof(uint32_t);
    auto vertexIndirectionViewIndex = static_cast<int32_t>(model.bufferViews.size());
    model.bufferViews.push_back(vertexIndirectionView);

    tinygltf::BufferView triangleView;
    triangleView.buffer = bufferIndex;
    triangleView.byteOffset = triangleBufferOffset;
    triangleView.byteLength = meshletTriangleBuffer.size();
    auto triangleViewIndex = static_cast<int32_t>(model.bufferViews.size());
    model.bufferViews.push_back(triangleView);

    tinygltf::Value::Object modelExtras;
    modelExtras["meshletBufferView"] = tinygltf::Value(meshletViewIndex);
    modelExtras["vertexIndirectionBufferView"] = tinygltf::Value(vertexIndirectionViewIndex);
    modelExtras["triangleBufferView"] = tinygltf::Value(triangleViewIndex);
    model.extras = tinygltf::Value(modelExtras);

    return loader.WriteGltfSceneToFile(&model, output.string(), true, true, true, true);
}
} // Editor
