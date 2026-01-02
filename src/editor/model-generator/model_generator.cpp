
// Created by William on 2026-01-02.
//

#include "model_generator.h"

#include <fstream>
#include <glm/glm.hpp>
#include <cgltf/cgltf.h>
#include <meshoptimizer.h>
#include <tracy/Tracy.hpp>
#include <json/nlohmann/json.hpp>
#include <utility>

#include "cgltf/cgltf_write.h"
#include "render/shaders/constants_interop.h"
#include "render/shaders/model_interop.h"


namespace Editor
{
MeshletBuildResult ModelGenerator::BuildMeshlets(std::vector<glm::vec3> vertices, std::vector<uint32_t> indices)
{
    ZoneScopedN("BuildMeshlets");
    size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES);
    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    std::vector<unsigned int> meshletVertices(vertices.size() * 3);
    std::vector<unsigned char> meshletTriangles(indices.size());

    // meshopt_optimizeVertexCache(indices, ...);
    // meshopt_optimizeVertexFetch(vertices, indices);

    // Building
    {
        ZoneScopedN("Building");
        std::vector<uint32_t> primitiveVertexPositions;
        meshlets.resize(meshopt_buildMeshlets(&meshlets[0], &meshletVertices[0], &meshletTriangles[0],
                                              indices.data(), indices.size(),
                                              reinterpret_cast<const float*>(vertices.data()), vertices.size(), sizeof(glm::vec3),
                                              MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES, 0.f));
    }

    // Optimize each meshlet's micro index buffer/vertex layout individually
    {
        ZoneScopedN("Optimize");
        for (auto& meshlet : meshlets) {
            meshopt_optimizeMeshlet(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
        }
    }

    // Trim the meshlet data to minimize waste for meshletVertices/meshletTriangles
    const meshopt_Meshlet& last = meshlets.back();
    meshletVertices.resize(last.vertex_offset + last.vertex_count);
    meshletTriangles.resize(last.triangle_offset + last.triangle_count * 3);

    std::vector<Meshlet> outputMeshlets{};
    outputMeshlets.reserve(meshlets.size());
    // Compute Meshlet Bounds
    {
        ZoneScopedN("ComputeBounds");
        for (meshopt_Meshlet& meshlet : meshlets) {
            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &meshletVertices[meshlet.vertex_offset],
                &meshletTriangles[meshlet.triangle_offset],
                meshlet.triangle_count,
                reinterpret_cast<const float*>(vertices.data()),
                vertices.size(),
                sizeof(glm::vec3)
            );

            outputMeshlets.push_back({
                .meshletBoundingSphere = glm::vec4(
                    bounds.center[0], bounds.center[1], bounds.center[2],
                    bounds.radius
                ),
                .coneApex = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]),
                .coneCutoff = bounds.cone_cutoff,

                .coneAxis = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]),
                .vertexOffset = 0, // Engine needs to += w/ meshletOffset

                .meshletVerticesOffset = meshlet.vertex_offset, // Engine needs to += w/ meshletVertexOffset
                .meshletTriangleOffset = meshlet.triangle_offset, // Engine needs to += w/ meshletTriangleOffset
                .meshletVerticesCount = meshlet.vertex_count,
                .meshletTriangleCount = meshlet.triangle_count,
            });
        }
    }

    return {outputMeshlets, meshletVertices, meshletTriangles};
}

MeshletBufferIndices ModelGenerator::AddMeshletBuffers(std::filesystem::path output, cgltf_data* data, const std::vector<Meshlet>& meshlets, const std::vector<uint32_t>& meshletVerts,
                                                       const std::vector<uint8_t>& meshletTris)
{
    const size_t meshletsSize = meshlets.size() * sizeof(meshopt_Meshlet);
    const size_t vertSize = meshletVerts.size() * sizeof(uint32_t);
    const size_t trisSize = meshletTris.size() * sizeof(uint8_t);

    const size_t meshletsAligned = (meshletsSize + 3) & ~3;
    const size_t vertAligned = (vertSize + 3) & ~3;
    const size_t totalSize = meshletsAligned + vertAligned + trisSize;

    // Allocate new buffer in data->buffers array
    cgltf_buffer* oldBufferPtr = data->buffers;
    data->buffers = static_cast<cgltf_buffer*>(realloc(data->buffers, (data->buffers_count + 1) * sizeof(cgltf_buffer)));

    if (data->buffers != oldBufferPtr) {
        for (cgltf_size i = 0; i < data->buffer_views_count; ++i) {
            if (data->buffer_views[i].buffer != nullptr) {
                ptrdiff_t offset = data->buffer_views[i].buffer - oldBufferPtr;
                data->buffer_views[i].buffer = data->buffers + offset;
            }
        }
    }

    cgltf_size newBufferIdx = data->buffers_count;
    data->buffers_count += 1;

    cgltf_buffer* newBuffer = &data->buffers[newBufferIdx];
    memset(newBuffer, 0, sizeof(cgltf_buffer));

    newBuffer->size = totalSize;
    newBuffer->data = malloc(totalSize);

    std::filesystem::path binPath = std::move(output);
    binPath.replace_extension(".meshlet.bin");

    std::ofstream binFile(binPath, std::ios::binary);
    binFile.write(reinterpret_cast<const char*>(meshlets.data()), meshletsSize);
    binFile.write(reinterpret_cast<const char*>(meshletVerts.data()), vertSize);
    binFile.write(reinterpret_cast<const char*>(meshletTris.data()), trisSize);
    binFile.close();

    newBuffer->size = totalSize;
    std::string filename = binPath.filename().string();
    newBuffer->uri = static_cast<char*>(malloc(filename.size() + 1));
    memcpy(newBuffer->uri, filename.c_str(), filename.size() + 1);

    /*memcpy(newBuffer->data, meshlets.data(), meshletsSize);
    memcpy(static_cast<uint8_t*>(newBuffer->data) + meshletsAligned, meshletVerts.data(), vertSize);
    memcpy(static_cast<uint8_t*>(newBuffer->data) + meshletsAligned + vertAligned, meshletTris.data(), trisSize);*/

    cgltf_size baseViewIdx = data->buffer_views_count;
    /*data->buffer_views = static_cast<cgltf_buffer_view*>(realloc(data->buffer_views, (data->buffer_views_count + 3) * sizeof(cgltf_buffer_view)));

    cgltf_buffer_view* meshletView = &data->buffer_views[data->buffer_views_count++];
    memset(meshletView, 0, sizeof(cgltf_buffer_view));
    meshletView->buffer = newBuffer;
    meshletView->offset = 0;
    meshletView->size = meshletsSize;

    cgltf_buffer_view* vertView = &data->buffer_views[data->buffer_views_count++];
    memset(vertView, 0, sizeof(cgltf_buffer_view));
    vertView->buffer = newBuffer;
    vertView->offset = meshletsAligned;
    vertView->size = vertSize;

    cgltf_buffer_view* triView = &data->buffer_views[data->buffer_views_count++];
    memset(triView, 0, sizeof(cgltf_buffer_view));
    triView->buffer = newBuffer;
    triView->offset = meshletsAligned + vertAligned;
    triView->size = trisSize;*/

    return {baseViewIdx, baseViewIdx + 1, baseViewIdx + 2};
}

bool ModelGenerator::ProcessModelsWithMeshlet(std::filesystem::path input, std::filesystem::path output)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, input.string().c_str(), &data);
    if (result != cgltf_result_success) {
        return false;
    }

    result = cgltf_load_buffers(&options, data, input.string().c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        cgltf_mesh& mesh = data->meshes[meshIndex];

        for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex) {
            cgltf_primitive& primitive = mesh.primitives[primIndex];

            // Find POSITION attribute
            cgltf_accessor* posAccessor = nullptr;
            for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
                if (primitive.attributes[i].type == cgltf_attribute_type_position) {
                    posAccessor = primitive.attributes[i].data;
                    break;
                }
            }

            if (posAccessor == nullptr) {
                cgltf_free(data);
                return false;
            }

            positions.clear();
            positions.resize(posAccessor->count);

            for (cgltf_size i = 0; i < posAccessor->count; ++i) {
                float pos[3];
                cgltf_accessor_read_float(posAccessor, i, pos, 3);
                positions[i] = {pos[0], pos[1], pos[2]};
            }

            cgltf_accessor* indexAccessor = primitive.indices;
            indices.clear();
            indices.resize(indexAccessor->count);

            for (cgltf_size i = 0; i < indexAccessor->count; ++i) {
                indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(indexAccessor, i));
            }

            auto [meshlets, meshletVertices, meshletTriangles] = BuildMeshlets(positions, indices);

             auto [meshletBufferIdx, meshletVertexIdx, meshletTriangleIdx] = AddMeshletBuffers(output, data, meshlets, meshletVertices, meshletTriangles);
            nlohmann::json j = {
                {
                    "willEngine_meshlets", {
                        {"meshletView", 0}, //meshletBufferIdx},
                        {"vertexView", 1}, //meshletVertexIdx},
                        {"triangleView", 2}, //meshletTriangleIdx},
                        {"meshletCount", meshlets.size()}
                    }
                }
            };

            std::string jsonStr = j.dump();
            primitive.extras.data = static_cast<char*>(malloc(jsonStr.size() + 1));
            memcpy(primitive.extras.data, jsonStr.c_str(), jsonStr.size() + 1);
        }
    }

    options.type = cgltf_file_type_glb;
    result = cgltf_write_file(&options, output.string().c_str(), data);
    cgltf_free(data);
    //return result == cgltf_result_success;

    result = cgltf_parse_file(&options, output.string().c_str(), &data);
    if (result != cgltf_result_success) {
        return false;
    }

    return true;


}
} // Editor
