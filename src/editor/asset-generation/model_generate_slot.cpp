//
// Created by William on 2026-02-02.
//

#include "model_generate_slot.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <meshoptimizer/src/meshoptimizer.h>

#include "asset_generator.h"
#include "platform/paths.h"
#include "render/model/model_format.h"
#include "render/model/model_serialization.h"
#include "render/shaders/constants_interop.h"
#include "tracy/Tracy.hpp"

namespace Editor
{
ModelGenerateSlot::ModelGenerateSlot() = default;

ModelGenerateSlot::~ModelGenerateSlot() = default;

void ModelGenerateSlot::Initialize(
    int32_t slotIndex,
    enki::TaskScheduler* _scheduler,
    AssetGenerator* _generator,
    WillModelGenerationProgress* _progress,
    std::function<void(bool success, ModelGenerateSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    generator = _generator;
    progress = _progress;
    temporaryPath = Platform::GetExecutablePath() / "temp" / ("model_gen_" + std::to_string(slotIndex));
    _notifyCallback = std::move(notifyCallback);
}

void ModelGenerateSlot::Launch(ModelGenerateSlotHandle _slotHandle, const std::filesystem::path& _gltfPath, const std::filesystem::path& _outputPath)
{
    gltfPath.clear();
    slotHandle = _slotHandle;
    gltfPath = _gltfPath;
    outputPath = _outputPath;

    if (task && !task->GetIsComplete()) {
        scheduler->WaitforTask(task.get());
    }

    task = std::make_unique<GenerateTask>();
    task->taskSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void ModelGenerateSlot::Clear()
{
    outputPath.clear();
    rawModel = {};
    sortedNodes.clear();
    visited.clear();
}

void ModelGenerateSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    taskSlot->progress->loadingState.store(WillModelGenerationProgress::LOADING_GLTF, std::memory_order_release);
    taskSlot->progress->value.store(0, std::memory_order_release);

    if (!taskSlot->LoadGltf()) {
        taskSlot->progress->loadingState.store(WillModelGenerationProgress::FAILED, std::memory_order_release);
        taskSlot->progress->value.store(0, std::memory_order_release);
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        return;
    }

    taskSlot->progress->loadingState.store(WillModelGenerationProgress::WRITING_WILL_MODEL, std::memory_order_release);
    taskSlot->progress->value.store(50, std::memory_order_release);

    if (!taskSlot->WriteWillModel()) {
        taskSlot->progress->loadingState.store(WillModelGenerationProgress::FAILED, std::memory_order_release);
        taskSlot->progress->value.store(0, std::memory_order_release);
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        return;
    }

    taskSlot->progress->loadingState.store(WillModelGenerationProgress::SUCCESS, std::memory_order_release);
    taskSlot->progress->value.store(100, std::memory_order_release);
    taskSlot->_notifyCallback(true, taskSlot->slotHandle);
}

bool ModelGenerateSlot::LoadGltf()
{
    ZoneScopedN("LoadGltf");

    int32_t _progress = 0;
    int32_t stepDiff = 50 / 9;

    fastgltf::Parser parser{fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform};
    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember
                                 | fastgltf::Options::AllowDouble
                                 | fastgltf::Options::LoadExternalBuffers
                                 | fastgltf::Options::LoadExternalImages;

    auto gltfFile = fastgltf::MappedGltfFile::FromPath(gltfPath);
    if (!static_cast<bool>(gltfFile)) {
        SPDLOG_ERROR("Failed to open glTF file ({}): {}", gltfPath.filename().string(), getErrorMessage(gltfFile.error()));
        return false;
    }

    auto load = parser.loadGltf(gltfFile.get(), gltfPath.parent_path(), gltfOptions);
    if (!load) {
        SPDLOG_ERROR("Failed to load glTF: {}", to_underlying(load.error()));
        return false;
    }

    fastgltf::Asset gltf = std::move(load.get());
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);

    // Samplers
    rawModel.name = gltfPath.filename().string();
    rawModel.samplerInfos.reserve(gltf.samplers.size());
    for (const fastgltf::Sampler& gltfSampler : gltf.samplers) {
        Engine::SamplerDesc desc{};
        desc.maxLod = VK_LOD_CLAMP_NONE;
        desc.minLod = 0;
        desc.magFilter = ExtractFilter(gltfSampler.magFilter.value_or(fastgltf::Filter::Nearest));
        desc.minFilter = ExtractFilter(gltfSampler.minFilter.value_or(fastgltf::Filter::Nearest));
        desc.mipmapMode = ExtractMipmapMode(gltfSampler.minFilter.value_or(fastgltf::Filter::Linear));
        rawModel.samplerInfos.push_back(desc);
    }
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);

    rawModel.images.reserve(gltf.images.size());
    if (!gltf.images.empty()) {

        unsigned char* stbiData = nullptr;
        int32_t width, height, nrChannels;

        std::filesystem::path parentPath = gltfPath.parent_path();
        for (const fastgltf::Image& gltfImage : gltf.images) {
            std::visit(
                fastgltf::visitor{
                    [&](auto& arg) {},
                    [&](const fastgltf::sources::URI& fileName) {
                        if (fileName.fileByteOffset != 0) {
                            SPDLOG_ERROR("File byte offset is not currently supported.");
                            return;
                        }
                        if (!fileName.uri.isLocalPath()) {
                            SPDLOG_ERROR("Loading non-local files is not currently supported.");
                            return;
                        }
                        const std::wstring widePath(fileName.uri.path().begin(), fileName.uri.path().end());
                        const std::filesystem::path fullPath = parentPath / widePath;
                        stbiData = stbi_load(fullPath.string().c_str(), &width, &height, &nrChannels, 4);
                    },
                    [&](const fastgltf::sources::Array& vector) {
                        if (vector.bytes.size() > 30) {
                            std::string_view strData(reinterpret_cast<const char*>(vector.bytes.data()), std::min(size_t(100), vector.bytes.size()));
                            if (strData.find("https://git-lfs.github.com/spec") != std::string_view::npos) {
                                SPDLOG_ERROR("Git LFS pointer detected. Run `git lfs pull` to retrieve files.");
                                return;
                            }
                        }
                        stbiData = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(vector.bytes.data()), static_cast<int>(vector.bytes.size()), &width, &height, &nrChannels, 4);
                    },
                    [&](const fastgltf::sources::BufferView& view) {
                        const fastgltf::BufferView& bufferView = gltf.bufferViews[view.bufferViewIndex];
                        const fastgltf::Buffer& buffer = gltf.buffers[bufferView.bufferIndex];
                        std::visit(fastgltf::visitor{
                                       [](auto&) {},
                                       [&](const fastgltf::sources::Array& vector) {
                                           stbiData = stbi_load_from_memory(
                                               reinterpret_cast<const stbi_uc*>(vector.bytes.data() + bufferView.byteOffset),
                                               static_cast<int>(bufferView.byteLength),
                                               &width, &height, &nrChannels, 4);
                                       }
                                   }, buffer.data);
                    }
                }, gltfImage.data);

            if (!stbiData) { break; }

            RawImage image{};
            image.w = width;
            image.h = height;
            image.bpp = 4;
            size_t size = image.w * image.h * 4;
            image.imageData = std::make_unique<uint8_t[]>(size);

            memcpy(image.imageData.get(), stbiData, size);
            stbi_image_free(stbiData);
            stbiData = nullptr;

            rawModel.images.push_back(std::move(image));
        }

        if (rawModel.images.size() != gltf.images.size()) {
            rawModel.images.clear();
            SPDLOG_ERROR("Mismatch of loaded images and expected images in the gltf");
            return false;
        }
    }


    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);

    // Materials
    rawModel.materials.reserve(gltf.materials.size());
    for (int32_t i = 0; i < gltf.materials.size(); ++i) {
        Engine::Material mat{};
        mat.name = rawModel.name + "_material_" + std::to_string(i);
        // mat.id             not relevant for model-based materials
        // mat.sourcePath     not relevant for model-based materials
        // mat.pipelineID = ; not yet used
        mat.immutable = true;
        mat.props = ExtractMaterial(gltf, gltf.materials[i]);
        rawModel.materials.emplace_back(mat);
    }
    for (const fastgltf::Material& gltfMaterial : gltf.materials) {}
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Meshes
    // WillModel stores as SkinnedVertex, when loading, the vertices will be loaded to different buffers depending on whether the model is a skeletal mesh.
    std::vector<Vertex> primitiveVertices{};
    std::vector<uint32_t> primitiveIndices{};
    rawModel.allMeshes.reserve(gltf.meshes.size());
    for (fastgltf::Mesh& mesh : gltf.meshes) {
        Render::MeshInformation meshData{};
        meshData.name = mesh.name;
        meshData.primitiveProperties.reserve(mesh.primitives.size());
        rawModel.primitives.reserve(rawModel.primitives.size() + mesh.primitives.size());

        for (fastgltf::Primitive& p : mesh.primitives) {
            MeshletPrimitive primitiveData{};
            int32_t materialIndex{-1};

            primitiveIndices.clear();
            primitiveVertices.clear();

            // Extract accessor data
            {
                if (p.materialIndex.has_value()) {
                    materialIndex = static_cast<int32_t>(p.materialIndex.value());
                    primitiveData.bHasTransparent = (static_cast<Render::MaterialType>(rawModel.materials[materialIndex].props.alphaProperties.y) == Render::MaterialType::BLEND);
                }


                // INDICES
                const fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
                primitiveIndices.reserve(indexAccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, [&](const std::uint32_t idx) {
                    primitiveIndices.push_back(idx);
                });

                // POSITION (REQUIRED)
                const fastgltf::Attribute* positionIt = p.findAttribute("POSITION");
                const fastgltf::Accessor& posAccessor = gltf.accessors[positionIt->accessorIndex];
                primitiveVertices.resize(posAccessor.count);

                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, posAccessor, [&](fastgltf::math::fvec3 v, const size_t index) {
                    primitiveVertices[index] = {};
                    primitiveVertices[index].position = {v.x(), v.y(), v.z()};
                    primitiveVertices[index].color = {1.0f, 1.0f, 1.0f, 1.0f};
                    primitiveVertices[index].normal = {0.0f, 0.0f, 1.0f};
                    primitiveVertices[index].tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                });


                // NORMALS
                const fastgltf::Attribute* normals = p.findAttribute("NORMAL");
                if (normals != p.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, gltf.accessors[normals->accessorIndex], [&](fastgltf::math::fvec3 n, const size_t index) {
                        primitiveVertices[index].normal = {n.x(), n.y(), n.z()};
                    });
                }

                // TANGENTS
                const fastgltf::Attribute* tangents = p.findAttribute("TANGENT");
                if (tangents != p.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, gltf.accessors[tangents->accessorIndex], [&](fastgltf::math::fvec4 t, const size_t index) {
                        primitiveVertices[index].tangent = {t.x(), t.y(), t.z(), t.w()};
                    });
                }

                // Skinned Rendering will be done in another model format
                // // JOINTS_0
                // const fastgltf::Attribute* joints0 = p.findAttribute("JOINTS_0");
                // if (joints0 != p.attributes.end()) {
                //     fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(gltf, gltf.accessors[joints0->accessorIndex], [&](fastgltf::math::uvec4 j, const size_t index) {
                //         primitiveVertices[index].joints = {j.x(), j.y(), j.z(), j.w()};
                //     });
                // }
                //
                // // WEIGHTS_0
                // const fastgltf::Attribute* weights0 = p.findAttribute("WEIGHTS_0");
                // if (weights0 != p.attributes.end()) {
                //     fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, gltf.accessors[weights0->accessorIndex], [&](fastgltf::math::fvec4 w, const size_t index) {
                //         primitiveVertices[index].weights = {w.x(), w.y(), w.z(), w.w()};
                //     });
                // }

                // UV
                const fastgltf::Attribute* uvs = p.findAttribute("TEXCOORD_0");
                if (uvs != p.attributes.end()) {
                    const fastgltf::Accessor& uvAccessor = gltf.accessors[uvs->accessorIndex];
                    switch (uvAccessor.componentType) {
                        case fastgltf::ComponentType::Byte:
                            fastgltf::iterateAccessorWithIndex<fastgltf::math::s8vec2>(gltf, uvAccessor, [&](fastgltf::math::s8vec2 uv, const size_t index) {
                                // f = max(c / 127.0, -1.0)
                                float u = std::max(static_cast<float>(uv.x()) / 127.0f, -1.0f);
                                float v = std::max(static_cast<float>(uv.y()) / 127.0f, -1.0f);
                                primitiveVertices[index].texcoordU = u;
                                primitiveVertices[index].texcoordV = v;
                            });
                            break;
                        case fastgltf::ComponentType::UnsignedByte:
                            fastgltf::iterateAccessorWithIndex<fastgltf::math::u8vec2>(gltf, uvAccessor, [&](fastgltf::math::u8vec2 uv, const size_t index) {
                                // f = c / 255.0
                                float u = static_cast<float>(uv.x()) / 255.0f;
                                float v = static_cast<float>(uv.y()) / 255.0f;
                                primitiveVertices[index].texcoordU = u;
                                primitiveVertices[index].texcoordV = v;
                            });
                            break;
                        case fastgltf::ComponentType::Short:
                            fastgltf::iterateAccessorWithIndex<fastgltf::math::s16vec2>(gltf, uvAccessor, [&](fastgltf::math::s16vec2 uv, const size_t index) {
                                // f = max(c / 32767.0, -1.0)
                                float u = std::max(
                                    static_cast<float>(uv.x()) / 32767.0f, -1.0f);
                                float v = std::max(
                                    static_cast<float>(uv.y()) / 32767.0f, -1.0f);
                                primitiveVertices[index].texcoordU = u;
                                primitiveVertices[index].texcoordV = v;
                            });
                            break;
                        case fastgltf::ComponentType::UnsignedShort:
                            fastgltf::iterateAccessorWithIndex<fastgltf::math::u16vec2>(gltf, uvAccessor, [&](fastgltf::math::u16vec2 uv, const size_t index) {
                                // f = c / 65535.0
                                float u = static_cast<float>(uv.x()) / 65535.0f;
                                float v = static_cast<float>(uv.y()) / 65535.0f;
                                primitiveVertices[index].texcoordU = u;
                                primitiveVertices[index].texcoordV = v;
                            });
                            break;
                        case fastgltf::ComponentType::Float:
                            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(gltf, uvAccessor, [&](fastgltf::math::fvec2 uv, const size_t index) {
                                primitiveVertices[index].texcoordU = uv.x();
                                primitiveVertices[index].texcoordV = uv.y();
                            });
                            break;
                        default:
                            fmt::print("Unsupported UV component type: {}\n", static_cast<int>(uvAccessor.componentType));
                            break;
                    }
                }

                // VERTEX COLOR
                const fastgltf::Attribute* colors = p.findAttribute("COLOR_0");
                if (colors != p.attributes.end()) {
                    auto& accessor = gltf.accessors[colors->accessorIndex];
                    switch (accessor.type) {
                        case fastgltf::AccessorType::Vec3:
                            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, accessor, [&](const fastgltf::math::fvec3& color, const size_t index) {
                                primitiveVertices[index].color = {color.x(), color.y(), color.z(), 1.0f};
                            });
                            break;
                        case fastgltf::AccessorType::Vec4:
                            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, accessor, [&](const fastgltf::math::fvec4& color, const size_t index) {
                                primitiveVertices[index].color = {color.x(), color.y(), color.z(), color.w()};
                            });
                            break;
                        default:
                            break;
                    }
                }
            }

            // Optimize Vertex and Index Buffer.
            {
                ZoneScopedN("Optimize Mesh");

                size_t indexCount = primitiveIndices.size();
                size_t vertexCount = primitiveVertices.size();
                std::vector<uint32_t> remap(vertexCount);

                size_t uniqueVertices; {
                    ZoneScopedN("Generate Vertex Remap");
                    uniqueVertices = meshopt_generateVertexRemap(
                        &remap[0],
                        primitiveIndices.data(),
                        indexCount,
                        primitiveVertices.data(),
                        vertexCount,
                        sizeof(Vertex));
                } {
                    ZoneScopedN("Remap Buffers");
                    std::vector<uint32_t> remappedIndices(primitiveIndices.size());
                    meshopt_remapIndexBuffer(
                        remappedIndices.data(),
                        primitiveIndices.data(),
                        primitiveIndices.size(),
                        remap.data());

                    std::vector<Vertex> remappedVertices(uniqueVertices);
                    meshopt_remapVertexBuffer(
                        remappedVertices.data(),
                        primitiveVertices.data(),
                        primitiveVertices.size(),
                        sizeof(Vertex),
                        remap.data()
                    );

                    primitiveIndices = std::move(remappedIndices);
                    primitiveVertices = std::move(remappedVertices);
                } {
                    ZoneScopedN("Optimize Vertex Cache");
                    meshopt_optimizeVertexCache(
                        primitiveIndices.data(),
                        primitiveIndices.data(),
                        primitiveIndices.size(),
                        primitiveVertices.size()
                    );
                } {
                    ZoneScopedN("Optimize Overdraw");
                    meshopt_optimizeOverdraw(
                        primitiveIndices.data(),
                        primitiveIndices.data(),
                        primitiveIndices.size(),
                        &primitiveVertices[0].position.x,
                        primitiveVertices.size(),
                        sizeof(Vertex),
                        1.05f
                    );
                } {
                    ZoneScopedN("Optimize Vertex Fetch");
                    meshopt_optimizeVertexFetch(
                        primitiveVertices.data(),
                        primitiveIndices.data(),
                        primitiveIndices.size(),
                        primitiveVertices.data(),
                        primitiveVertices.size(),
                        sizeof(Vertex)
                    );
                }
            }

            std::array<std::vector<uint32_t>, LOD_COUNT> lodIndices{};
            std::array<std::vector<meshopt_Meshlet>, LOD_COUNT> lodMeshlets{};
            std::array<std::vector<uint32_t>, LOD_COUNT> lodMeshletVertices{};
            std::array<std::vector<uint8_t>, LOD_COUNT> lodMeshletTriangles{};

            //
            {
                lodIndices[0] = primitiveIndices;

                for (uint32_t lod = 1; lod < LOD_COUNT; ++lod) {
                    ZoneScopedN("Generate LOD");
                    TracyMessageL(std::to_string(lod).c_str());

                    constexpr float thresholds[LOD_COUNT - 1]{0.5f, 0.5f, 0.3f};
                    size_t target_index_count = size_t(lodIndices[lod - 1].size() * thresholds[lod - 1]);
                    target_index_count = target_index_count / 3 * 3;

                    lodIndices[lod].resize(lodIndices[lod - 1].size());

                    size_t simplified_index_count = meshopt_simplify(
                        lodIndices[lod].data(),
                        lodIndices[lod - 1].data(),
                        lodIndices[lod - 1].size(),
                        &primitiveVertices[0].position.x,
                        primitiveVertices.size(),
                        sizeof(Vertex),
                        target_index_count,
                        0.01f
                    );
                    lodIndices[lod].resize(simplified_index_count);
                }

                for (uint32_t lod = 0; lod < LOD_COUNT; ++lod) {
                    ZoneScopedN("Build Meshlets LOD");
                    TracyMessageL(std::to_string(lod).c_str());

                    size_t max_meshlets = meshopt_buildMeshletsBound(
                        lodIndices[lod].size(),
                        MESHLET_MAX_VERTICES,
                        MESHLET_MAX_TRIANGLES
                    );

                    lodMeshlets[lod].resize(max_meshlets);
                    lodMeshletVertices[lod].resize(max_meshlets * MESHLET_MAX_VERTICES);
                    lodMeshletTriangles[lod].resize(max_meshlets * MESHLET_MAX_TRIANGLES * 3);

                    size_t meshlet_count = meshopt_buildMeshlets(
                        lodMeshlets[lod].data(),
                        lodMeshletVertices[lod].data(),
                        lodMeshletTriangles[lod].data(),
                        lodIndices[lod].data(),
                        lodIndices[lod].size(),
                        &primitiveVertices[0].position.x,
                        primitiveVertices.size(),
                        sizeof(Vertex),
                        MESHLET_MAX_VERTICES,
                        MESHLET_MAX_TRIANGLES,
                        0.0f
                    );

                    lodMeshlets[lod].resize(meshlet_count);

                    // Optimize each meshlet
                    {
                        ZoneScopedN("Optimize Meshlets");
                        for (auto& meshlet : lodMeshlets[lod]) {
                            meshopt_optimizeMeshlet(
                                &lodMeshletVertices[lod][meshlet.vertex_offset],
                                &lodMeshletTriangles[lod][meshlet.triangle_offset],
                                meshlet.triangle_count,
                                meshlet.vertex_count
                            );
                        }
                    }

                    // Trim
                    const meshopt_Meshlet& last = lodMeshlets[lod].back();
                    lodMeshletVertices[lod].resize(last.vertex_offset + last.vertex_count);
                    lodMeshletTriangles[lod].resize(last.triangle_offset + last.triangle_count * 3);
                }
            }

            primitiveData.boundingSphere = GenerateBoundingSphere(primitiveVertices);

            // Same for all LODs (without LOD streaming anyway)
            uint32_t vertexOffset = rawModel.vertices.size();
            rawModel.vertices.insert(rawModel.vertices.end(), primitiveVertices.begin(), primitiveVertices.end());


            for (uint32_t lod = 0; lod < LOD_COUNT; ++lod) {
                primitiveData.meshletOffset[lod] = rawModel.meshlets.size();
                primitiveData.meshletCount[lod] = lodMeshlets[lod].size();

                uint32_t meshletVertexOffset = rawModel.meshletVertices.size();
                uint32_t meshletTrianglesOffset = rawModel.meshletTriangles.size();
                rawModel.meshletVertices.insert(rawModel.meshletVertices.end(), lodMeshletVertices[lod].begin(), lodMeshletVertices[lod].end());
                rawModel.meshletTriangles.insert(rawModel.meshletTriangles.end(), lodMeshletTriangles[lod].begin(), lodMeshletTriangles[lod].end());

                //
                {
                    ZoneScopedN("ComputeMeshletBounds");
                    for (meshopt_Meshlet& meshlet : lodMeshlets[lod]) {
                        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                            &lodMeshletVertices[lod][meshlet.vertex_offset],
                            &lodMeshletTriangles[lod][meshlet.triangle_offset],
                            meshlet.triangle_count,
                            reinterpret_cast<const float*>(primitiveVertices.data()),
                            primitiveVertices.size(),
                            sizeof(Vertex)
                        );

                        rawModel.meshlets.push_back({
                            .meshletBoundingSphere = glm::vec4(
                                bounds.center[0], bounds.center[1], bounds.center[2],
                                bounds.radius
                            ),
                            .coneApex = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]),
                            .coneCutoff = bounds.cone_cutoff,

                            .coneAxis = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]),
                            .vertexOffset = vertexOffset,

                            .meshletVertexOffset = meshletVertexOffset + meshlet.vertex_offset,
                            .meshletTriangleOffset = meshletTrianglesOffset + meshlet.triangle_offset,
                            .meshletVertexCount = meshlet.vertex_count,
                            .meshletTriangleCount = meshlet.triangle_count,
                        });
                    }
                }
            }

            meshData.primitiveProperties.emplace_back(static_cast<uint32_t>(rawModel.primitives.size()), materialIndex);
            rawModel.primitives.push_back(primitiveData);
        }

        rawModel.allMeshes.push_back(meshData);
    }
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Nodes
    rawModel.nodes.reserve(gltf.nodes.size());
    for (const fastgltf::Node& node : gltf.nodes) {
        Render::Node _node{};
        _node.name = node.name;

        if (node.meshIndex.has_value()) {
            _node.meshIndex = static_cast<int>(*node.meshIndex);
        }

        std::visit(
            fastgltf::visitor{
                [&](fastgltf::math::fmat4x4 matrix) {
                    glm::mat4 glmMatrix;
                    for (int i = 0; i < 4; ++i) {
                        for (int j = 0; j < 4; ++j) {
                            glmMatrix[i][j] = matrix[i][j];
                        }
                    }

                    _node.localTranslation = glm::vec3(glmMatrix[3]);
                    _node.localRotation = glm::quat_cast(glmMatrix);
                    _node.localScale = glm::vec3(
                        glm::length(glm::vec3(glmMatrix[0])),
                        glm::length(glm::vec3(glmMatrix[1])),
                        glm::length(glm::vec3(glmMatrix[2]))
                    );
                },
                [&](fastgltf::TRS transform) {
                    _node.localTranslation = {transform.translation[0], transform.translation[1], transform.translation[2]};
                    _node.localRotation = {transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]};
                    _node.localScale = {transform.scale[0], transform.scale[1], transform.scale[2]};
                }
            }
            , node.transform
        );
        rawModel.nodes.push_back(_node);
    }
    for (int i = 0; i < gltf.nodes.size(); i++) {
        for (std::size_t& child : gltf.nodes[i].children) {
            rawModel.nodes[child].parent = i;
        }
    }
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Skins
    // Only import first skin
    if (!gltf.skins.empty()) {
        fastgltf::Skin& skins = gltf.skins[0];

        if (gltf.skins.size() > 1) {
            SPDLOG_WARN("Model has {} skins but only loading first skin", gltf.skins.size());
        }

        if (skins.inverseBindMatrices.has_value()) {
            const fastgltf::Accessor& inverseBindAccessor = gltf.accessors[skins.inverseBindMatrices.value()];
            rawModel.inverseBindMatrices.resize(inverseBindAccessor.count);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(gltf, inverseBindAccessor, [&](const fastgltf::math::fmat4x4& m, const size_t index) {
                glm::mat4 glmMatrix;
                for (int col = 0; col < 4; ++col) {
                    for (int row = 0; row < 4; ++row) {
                        glmMatrix[col][row] = m[col][row];
                    }
                }
                rawModel.inverseBindMatrices[index] = glmMatrix;
            });

            for (int32_t i = 0; i < skins.joints.size(); ++i) {
                rawModel.nodes[skins.joints[i]].inverseBindIndex = i;
            }
        }
    }
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Node processing
    std::vector<uint32_t> nodeRemap{};
    TopologicalSortNodes(rawModel.nodes, nodeRemap);
    for (size_t i = 0; i < rawModel.nodes.size(); ++i) {
        uint32_t depth = 0;
        uint32_t currentParent = rawModel.nodes[i].parent;

        while (currentParent != ~0u) {
            depth++;
            currentParent = rawModel.nodes[currentParent].parent;
        }

        rawModel.nodes[i].depth = depth;
    }
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Animations
    rawModel.animations.reserve(gltf.animations.size());
    for (fastgltf::Animation& gltfAnim : gltf.animations) {
        Render::Animation anim{};
        anim.name = gltfAnim.name;

        for (fastgltf::AnimationSampler& animSampler : gltfAnim.samplers) {
            Render::AnimationSampler sampler;

            const fastgltf::Accessor& inputAccessor = gltf.accessors[animSampler.inputAccessor];
            sampler.timestamps.resize(inputAccessor.count);
            fastgltf::iterateAccessorWithIndex<float>(gltf, inputAccessor, [&](float value, size_t idx) {
                sampler.timestamps[idx] = value;
            });

            const fastgltf::Accessor& outputAccessor = gltf.accessors[animSampler.outputAccessor];
            sampler.values.resize(outputAccessor.count * fastgltf::getNumComponents(outputAccessor.type));
            if (outputAccessor.type == fastgltf::AccessorType::Vec3) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, outputAccessor,
                                                                          [&](const fastgltf::math::fvec3& value, size_t idx) {
                                                                              size_t baseIdx = idx * 3; // Calculate flat base index
                                                                              sampler.values[baseIdx + 0] = value.x();
                                                                              sampler.values[baseIdx + 1] = value.y();
                                                                              sampler.values[baseIdx + 2] = value.z();
                                                                          });
            }
            else if (outputAccessor.type == fastgltf::AccessorType::Vec4) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, outputAccessor,
                                                                          [&](const fastgltf::math::fvec4& value, size_t idx) {
                                                                              size_t baseIdx = idx * 4; // Calculate flat base index
                                                                              sampler.values[baseIdx + 0] = value.x();
                                                                              sampler.values[baseIdx + 1] = value.y();
                                                                              sampler.values[baseIdx + 2] = value.z();
                                                                              sampler.values[baseIdx + 3] = value.w();
                                                                          });
            }
            else if (outputAccessor.type == fastgltf::AccessorType::Scalar) {
                fastgltf::iterateAccessorWithIndex<float>(gltf, outputAccessor,
                                                          [&](float value, size_t idx) {
                                                              sampler.values[idx] = value; // This one is fine since it's 1 component
                                                          });
            }

            switch (animSampler.interpolation) {
                case fastgltf::AnimationInterpolation::Linear:
                    sampler.interpolation = Render::AnimationSampler::Interpolation::Linear;
                    break;
                case fastgltf::AnimationInterpolation::Step:
                    sampler.interpolation = Render::AnimationSampler::Interpolation::Step;
                    break;
                case fastgltf::AnimationInterpolation::CubicSpline:
                    sampler.interpolation = Render::AnimationSampler::Interpolation::CubicSpline;
                    break;
            }

            anim.samplers.push_back(std::move(sampler));
        }

        anim.channels.reserve(gltfAnim.channels.size());
        for (auto& gltfChannel : gltfAnim.channels) {
            Render::AnimationChannel channel{};
            channel.samplerIndex = gltfChannel.samplerIndex;
            channel.targetNodeIndex = nodeRemap[gltfChannel.nodeIndex.value()];

            switch (gltfChannel.path) {
                case fastgltf::AnimationPath::Translation:
                    channel.targetPath = Render::AnimationChannel::TargetPath::Translation;
                    break;
                case fastgltf::AnimationPath::Rotation:
                    channel.targetPath = Render::AnimationChannel::TargetPath::Rotation;
                    break;
                case fastgltf::AnimationPath::Scale:
                    channel.targetPath = Render::AnimationChannel::TargetPath::Scale;
                    break;
                case fastgltf::AnimationPath::Weights:
                    channel.targetPath = Render::AnimationChannel::TargetPath::Weights;
                    break;
            }

            anim.channels.push_back(channel);
        }

        anim.duration = 0.0f;
        for (const auto& sampler : anim.samplers) {
            if (!sampler.timestamps.empty()) {
                anim.duration = std::max(anim.duration, sampler.timestamps.back());
            }
        }

        rawModel.animations.push_back(std::move(anim));
    }
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    rawModel.bSuccessfullyLoaded = true;
    return true;
}

bool ModelGenerateSlot::WriteWillModel()
{
    ZoneScopedN("WriteWillModel");

    if (temporaryPath.empty()) {
        SPDLOG_ERROR("Failed to write willmodel because temporary directory is not set.");
        return false;
    }

    //
    {
        ZoneScopedN("CleanupTempDirectory");
        if (std::filesystem::exists(temporaryPath)) {
            std::filesystem::remove_all(temporaryPath);
        }
        std::filesystem::create_directories(temporaryPath);
    }

    std::vector<DXGI_FORMAT> preferredImageFormats;
    preferredImageFormats.resize(rawModel.images.size(), DXGI_FORMAT_BC7_UNORM);

    for (const auto& material : rawModel.materials) {
        // Color/emissive textures -> BC7 SRGB
        if (material.props.textureImageIndices.x >= 0) {
            preferredImageFormats[material.props.textureImageIndices.x] = DXGI_FORMAT_BC7_UNORM_SRGB;
        }
        if (material.props.textureImageIndices.w >= 0) {
            preferredImageFormats[material.props.textureImageIndices.w] = DXGI_FORMAT_BC7_UNORM_SRGB;
        }

        // Normal map -> BC5
        if (material.props.textureImageIndices.z >= 0) {
            preferredImageFormats[material.props.textureImageIndices.z] = DXGI_FORMAT_BC5_UNORM;
        }

        // Metallic-roughness -> BC7 (linear)
        if (material.props.textureImageIndices.y >= 0) {
            preferredImageFormats[material.props.textureImageIndices.y] = DXGI_FORMAT_BC7_UNORM;
        }

        // Occlusion -> BC4
        if (material.props.textureImageIndices2.x >= 0) {
            preferredImageFormats[material.props.textureImageIndices2.x] = DXGI_FORMAT_BC4_UNORM;
        }

        // Packed NRM (if used) -> BC7
        if (material.props.textureImageIndices2.y >= 0) {
            preferredImageFormats[material.props.textureImageIndices2.y] = DXGI_FORMAT_BC7_UNORM;
        }
    }

    std::vector<Engine::TextureID> textureIDs;
    textureIDs.reserve(rawModel.images.size());

    for (int32_t i = 0; i < rawModel.images.size(); ++i) {
        RawImage& image = rawModel.images[i];
        std::filesystem::path textureOutPath = gltfPath.parent_path() / "textures" / (gltfPath.stem().string() + "_texture_" + std::to_string(i) + ".wtexture");
        textureIDs.push_back(generator->RequestTextureGenerateFromMemory(std::move(image.imageData), image.w, image.h, image.bpp, textureOutPath, true, preferredImageFormats[i]));
    }

    auto texRef = [&](int idx) -> Engine::TextureID {
        return idx >= 0 && idx < static_cast<int>(textureIDs.size()) ? textureIDs[idx] : Engine::TextureID::INVALID;
    };
    auto sampDesc = [&](int idx) -> Engine::SamplerDesc {
        return idx >= 0 && idx < static_cast<int>(rawModel.samplerInfos.size()) ? rawModel.samplerInfos[idx] : Engine::SamplerDesc{};
    };

    for (auto& mat : rawModel.materials) {
        mat.textureRefs[0] = texRef(mat.props.textureImageIndices.x);
        mat.textureRefs[1] = texRef(mat.props.textureImageIndices.y);
        mat.textureRefs[2] = texRef(mat.props.textureImageIndices.z);
        mat.textureRefs[3] = texRef(mat.props.textureImageIndices.w);
        mat.textureRefs[4] = texRef(mat.props.textureImageIndices2.x);
        mat.textureRefs[5] = texRef(mat.props.textureImageIndices2.y);

        mat.samplerDesc[0] = sampDesc(mat.props.textureSamplerIndices.x);
        mat.samplerDesc[1] = sampDesc(mat.props.textureSamplerIndices.y);
        mat.samplerDesc[2] = sampDesc(mat.props.textureSamplerIndices.z);
        mat.samplerDesc[3] = sampDesc(mat.props.textureSamplerIndices.w);
        mat.samplerDesc[4] = sampDesc(mat.props.textureSamplerIndices2.x);
        mat.samplerDesc[5] = sampDesc(mat.props.textureSamplerIndices2.y);

        // Clear GLTF-local indices — bindless indices are resolved at load time from textureRefs
        mat.props.textureImageIndices = glm::ivec4(-1);
        mat.props.textureSamplerIndices = glm::ivec4(-1);
        mat.props.textureImageIndices2 = glm::ivec4(-1);
        mat.props.textureSamplerIndices2 = glm::ivec4(-1);
    }

    {
        ZoneScopedN("WriteModelBinary");
        std::ofstream binFile(temporaryPath / "model.bin", std::ios::binary);
        WriteModelBinary(binFile, rawModel);
        binFile.close();
    }


    // Create archive
    {
        ZoneScopedN("CreateArchive");

        Render::ModelWriter writer{outputPath};
        writer.AddFileFromDisk("model.bin", (temporaryPath / "model.bin").string(), Render::CompressionType::LZ4);

        // Nodes separate
        {
            std::ofstream nodesBinFile(temporaryPath / "nodes.bin", std::ios::binary);
            auto nodeCount = static_cast<uint32_t>(rawModel.nodes.size());
            nodesBinFile.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
            for (const auto& node : rawModel.nodes) {
                WriteNode(nodesBinFile, node);
            }
        }
        writer.AddFileFromDisk("nodes.bin", (temporaryPath / "nodes.bin").string(), Render::CompressionType::LZ4);

        uint32_t meshNodeCount = 0;
        for (const auto& node : rawModel.nodes) {
            if (node.meshIndex != ~0u) ++meshNodeCount;
        }
        writer.SetMetadata({static_cast<uint32_t>(rawModel.nodes.size()), meshNodeCount});

        if (!writer.Finalize()) {
            return false;
        }
    }

    progress->value.store(100, std::memory_order_release);
    return true;
}


VkFilter ModelGenerateSlot::ExtractFilter(fastgltf::Filter filter)
{
    switch (filter) {
        // nearest samplers
        case fastgltf::Filter::Nearest:
        case fastgltf::Filter::NearestMipMapNearest:
        case fastgltf::Filter::NearestMipMapLinear:
            return VK_FILTER_NEAREST;
        // linear samplers
        case fastgltf::Filter::Linear:
        case fastgltf::Filter::LinearMipMapNearest:
        case fastgltf::Filter::LinearMipMapLinear:
        default:
            return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode ModelGenerateSlot::ExtractMipmapMode(fastgltf::Filter filter)
{
    switch (filter) {
        case fastgltf::Filter::Nearest:
        case fastgltf::Filter::NearestMipMapNearest:
        case fastgltf::Filter::LinearMipMapNearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case fastgltf::Filter::Linear:
        case fastgltf::Filter::NearestMipMapLinear:
        case fastgltf::Filter::LinearMipMapLinear:
        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

MaterialProperties ModelGenerateSlot::ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial)
{
    MaterialProperties material{};
    material.colorFactor = glm::vec4(1.0f);
    material.metalRoughFactors = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    material.textureImageIndices = glm::ivec4(-1);
    material.textureSamplerIndices = glm::ivec4(-1);
    material.textureImageIndices2 = glm::ivec4(-1);
    material.textureSamplerIndices2 = glm::ivec4(-1);
    material.colorUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    material.metalRoughUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    material.normalUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    material.emissiveUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    material.occlusionUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    material.emissiveFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    material.alphaProperties = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);
    material.physicalProperties = glm::vec4(1.5f, 0.0f, 1.0f, 0.0f);

    material.colorFactor = glm::vec4(
        gltfMaterial.pbrData.baseColorFactor[0],
        gltfMaterial.pbrData.baseColorFactor[1],
        gltfMaterial.pbrData.baseColorFactor[2],
        gltfMaterial.pbrData.baseColorFactor[3]);

    material.metalRoughFactors.x = gltfMaterial.pbrData.metallicFactor;
    material.metalRoughFactors.y = gltfMaterial.pbrData.roughnessFactor;

    material.alphaProperties.x = gltfMaterial.alphaCutoff;
    material.alphaProperties.z = gltfMaterial.doubleSided ? 1.0f : 0.0f;
    material.alphaProperties.w = gltfMaterial.unlit ? 1.0f : 0.0f;

    switch (gltfMaterial.alphaMode) {
        case fastgltf::AlphaMode::Opaque:
            material.alphaProperties.y = static_cast<float>(Render::MaterialType::SOLID);
            break;
        case fastgltf::AlphaMode::Blend:
            material.alphaProperties.y = static_cast<float>(Render::MaterialType::BLEND);
            break;
        case fastgltf::AlphaMode::Mask:
            material.alphaProperties.y = static_cast<float>(Render::MaterialType::CUTOUT);
            break;
    }

    material.emissiveFactor = glm::vec4(
        gltfMaterial.emissiveFactor[0],
        gltfMaterial.emissiveFactor[1],
        gltfMaterial.emissiveFactor[2],
        gltfMaterial.emissiveStrength);

    material.physicalProperties.x = gltfMaterial.ior;
    material.physicalProperties.y = gltfMaterial.dispersion;

    // Handle edge cases for missing samplers/images
    auto fixTextureIndices = [](int& imageIdx, int& samplerIdx) {
        if (imageIdx == -1 && samplerIdx != -1) imageIdx = 0;
        if (samplerIdx == -1 && imageIdx != -1) samplerIdx = 0;
    };

    if (gltfMaterial.pbrData.baseColorTexture.has_value()) {
        LoadTextureIndicesAndUV(gltfMaterial.pbrData.baseColorTexture.value(), gltf, material.textureImageIndices.x, material.textureSamplerIndices.x, material.colorUvTransform);
        fixTextureIndices(material.textureImageIndices.x, material.textureSamplerIndices.x);
    }


    if (gltfMaterial.pbrData.metallicRoughnessTexture.has_value()) {
        LoadTextureIndicesAndUV(gltfMaterial.pbrData.metallicRoughnessTexture.value(), gltf, material.textureImageIndices.y, material.textureSamplerIndices.y, material.metalRoughUvTransform);
        fixTextureIndices(material.textureImageIndices.y, material.textureSamplerIndices.y);
    }

    if (gltfMaterial.normalTexture.has_value()) {
        LoadTextureIndicesAndUV(gltfMaterial.normalTexture.value(), gltf, material.textureImageIndices.z, material.textureSamplerIndices.z, material.normalUvTransform);
        material.physicalProperties.z = gltfMaterial.normalTexture->scale;
        fixTextureIndices(material.textureImageIndices.z, material.textureSamplerIndices.z);
    }

    if (gltfMaterial.emissiveTexture.has_value()) {
        LoadTextureIndicesAndUV(gltfMaterial.emissiveTexture.value(), gltf, material.textureImageIndices.w, material.textureSamplerIndices.w, material.emissiveUvTransform);
        fixTextureIndices(material.textureImageIndices.w, material.textureSamplerIndices.w);
    }

    if (gltfMaterial.occlusionTexture.has_value()) {
        LoadTextureIndicesAndUV(gltfMaterial.occlusionTexture.value(), gltf, material.textureImageIndices2.x, material.textureSamplerIndices2.x, material.occlusionUvTransform);
        material.physicalProperties.w = gltfMaterial.occlusionTexture->strength;
        fixTextureIndices(material.textureImageIndices2.x, material.textureSamplerIndices2.x);
    }

    if (gltfMaterial.packedNormalMetallicRoughnessTexture.has_value()) {
        SPDLOG_WARN("This renderer does not support packed normal metallic roughness texture.");
        //fixTextureIndices(material.textureImageIndices2.y, material.textureSamplerIndices2.y);
    }

    return material;
}

void ModelGenerateSlot::LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform)
{
    const size_t textureIndex = texture.textureIndex;

    if (gltf.textures[textureIndex].basisuImageIndex.has_value()) {
        imageIndex = gltf.textures[textureIndex].basisuImageIndex.value();
    }
    else if (gltf.textures[textureIndex].imageIndex.has_value()) {
        imageIndex = gltf.textures[textureIndex].imageIndex.value();
    }

    if (gltf.textures[textureIndex].samplerIndex.has_value()) {
        samplerIndex = gltf.textures[textureIndex].samplerIndex.value();
    }

    if (texture.transform) {
        const auto& transform = texture.transform;
        uvTransform.x = transform->uvScale[0];
        uvTransform.y = transform->uvScale[1];
        uvTransform.z = transform->uvOffset[0];
        uvTransform.w = transform->uvOffset[1];
    }
}

glm::vec4 ModelGenerateSlot::GenerateBoundingSphere(const std::vector<Vertex>& vertices)
{
    glm::vec3 center = {0, 0, 0};

    for (auto&& vertex : vertices) {
        center += vertex.position;
    }
    center /= static_cast<float>(vertices.size());


    float radius = glm::dot(vertices[0].position - center, vertices[0].position - center);
    for (size_t i = 1; i < vertices.size(); ++i) {
        radius = std::max(radius, glm::dot(vertices[i].position - center, vertices[i].position - center));
    }
    radius = std::nextafter(sqrtf(radius), std::numeric_limits<float>::max());

    return {center, radius};
}

void WriteModelBinary(std::ofstream& file, const RawGltfModel& model)
{
    Render::ModelBinaryHeader header{};
    header.vertexCount = static_cast<uint32_t>(model.vertices.size());
    header.meshletVertexCount = static_cast<uint32_t>(model.meshletVertices.size());
    header.meshletTriangleCount = static_cast<uint32_t>(model.meshletTriangles.size());
    header.meshletCount = static_cast<uint32_t>(model.meshlets.size());
    header.primitiveCount = static_cast<uint32_t>(model.primitives.size());
    header.materialCount = static_cast<uint32_t>(model.materials.size());
    header.meshCount = static_cast<uint32_t>(model.allMeshes.size());
    header.animationCount = static_cast<uint32_t>(model.animations.size());
    header.inverseBindMatrixCount = static_cast<uint32_t>(model.inverseBindMatrices.size());

    file.write(reinterpret_cast<const char*>(&header), sizeof(Render::ModelBinaryHeader));

    Render::WriteVector(file, model.vertices);
    Render::WriteVector(file, model.meshletVertices);
    Render::WriteVector(file, model.meshletTriangles);
    Render::WriteVector(file, model.meshlets);
    Render::WriteVector(file, model.primitives);

    for (const auto& mat : model.materials) {
        Render::WriteMaterial(file, mat);
    }

    for (const auto& mesh : model.allMeshes) {
        WriteMeshInformation(file, mesh);
    }

    for (const auto& anim : model.animations) {
        WriteAnimation(file, anim);
    }

    Render::WriteVector(file, model.inverseBindMatrices);
}

void ModelGenerateSlot::TopologicalSortNodes(std::vector<Render::Node>& nodes, std::vector<uint32_t>& oldToNew)
{
    oldToNew.resize(nodes.size());

    sortedNodes.clear();
    sortedNodes.reserve(nodes.size());

    visited.clear();
    visited.resize(nodes.size(), false);

    // Topological sort
    std::function<void(uint32_t)> visit = [&](uint32_t idx) {
        if (visited[idx]) return;
        visited[idx] = true;

        if (nodes[idx].parent != ~0u) {
            visit(nodes[idx].parent);
        }

        oldToNew[idx] = sortedNodes.size();
        sortedNodes.push_back(nodes[idx]);
    };

    for (uint32_t i = 0; i < nodes.size(); ++i) {
        visit(i);
    }

    for (auto& node : sortedNodes) {
        if (node.parent != ~0u) {
            node.parent = oldToNew[node.parent];
        }
    }

    nodes = std::move(sortedNodes);
}
} // Render
