//
// Created by William on 2026-02-02.
//

#include "static_model_generate_slot.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <meshoptimizer/src/meshoptimizer.h>

#include "asset_generator.h"
#include "core/containers/fixed_vector.h"
#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/model/model_format.h"
#include "engine/serialization/serialization.h"
#include "render/shaders/constants_interop.h"
#include "tracy/Tracy.hpp"

namespace Editor
{
StaticModelGenerateSlot::StaticModelGenerateSlot() = default;

StaticModelGenerateSlot::~StaticModelGenerateSlot() = default;

void StaticModelGenerateSlot::Initialize(
    Core::MemoryManager* _memoryManager,
    enki::TaskScheduler* _scheduler,
    AssetGenerator* _generator,
    StaticModelGenerationProgress* _progress,
    Core::InlineFunction<void(bool success, ModelGenerateSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    generator = _generator;
    progress = _progress;
    memoryManager = _memoryManager;
    rawModel.Init(&memoryManager->AssetsScratch());
    _notifyCallback = std::move(notifyCallback);
}

void StaticModelGenerateSlot::Launch(ModelGenerateSlotHandle _slotHandle, const Core::Path& _gltfPath, const Core::Path& _outputPath, uint64_t _modelId)
{
    gltfPath = Core::Path{};
    slotHandle = _slotHandle;
    gltfPath = _gltfPath;
    outputPath = _outputPath;
    modelId = _modelId;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }

    task.taskSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void StaticModelGenerateSlot::Clear()
{
    outputPath = Core::Path{};
    rawModel = {};
}

void StaticModelGenerateSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    taskSlot->progress->loadingState.store(StaticModelGenerationProgress::LOADING_GLTF, std::memory_order_release);
    taskSlot->progress->value.store(0, std::memory_order_release);

    if (!taskSlot->LoadGltf()) {
        taskSlot->progress->loadingState.store(StaticModelGenerationProgress::FAILED, std::memory_order_release);
        taskSlot->progress->value.store(0, std::memory_order_release);
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        return;
    }

    taskSlot->progress->loadingState.store(StaticModelGenerationProgress::WRITING_STATIC_MODEL, std::memory_order_release);
    taskSlot->progress->value.store(50, std::memory_order_release);

    if (!taskSlot->WriteStaticModel()) {
        taskSlot->progress->loadingState.store(StaticModelGenerationProgress::FAILED, std::memory_order_release);
        taskSlot->progress->value.store(0, std::memory_order_release);
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        return;
    }

    taskSlot->progress->loadingState.store(StaticModelGenerationProgress::SUCCESS, std::memory_order_release);
    taskSlot->progress->value.store(100, std::memory_order_release);
    taskSlot->_notifyCallback(true, taskSlot->slotHandle);
}

bool StaticModelGenerateSlot::LoadGltf()
{
    ZoneScopedN("LoadGltf");

    int32_t _progress = 0;
    int32_t stepDiff = 50 / 9;

    fastgltf::Parser parser{fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform};
    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember
                                 | fastgltf::Options::AllowDouble
                                 | fastgltf::Options::LoadExternalBuffers
                                 | fastgltf::Options::LoadExternalImages;

    // MEM: fastgltf forces the use of std::filesystem::path
    auto gltfSrcPath = std::filesystem::path(gltfPath.c_str());
    auto gltfFile = fastgltf::MappedGltfFile::FromPath(gltfSrcPath);
    if (!static_cast<bool>(gltfFile)) {
        LOG_ERROR(Asset, "Failed to open glTF file ({}): {}", gltfPath.Filename(), getErrorMessage(gltfFile.error()));
        return false;
    }

    // MEM: fastgltf forces the use of std::filesystem::path
    auto gltfParentPath = std::filesystem::path(gltfPath.Parent().c_str());
    auto load = parser.loadGltf(gltfFile.get(), gltfParentPath, gltfOptions);
    if (!load) {
        LOG_ERROR(Asset, "Failed to load glTF: {}", to_underlying(load.error()));
        return false;
    }

    // MEM: fastgltf does a bunch of heap allocs internally...
    // todo: replace with cgltf
    fastgltf::Asset gltf = std::move(load.get());
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);

    rawModel.name = Core::InlineString<128>(gltfPath.Filename());

    // Samplers
    if (!gltf.samplers.empty()) {
        rawModel.samplerInfos = Core::HeapArray<Engine::SamplerDesc>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.samplers.size());
        for (int i = 0; i < gltf.samplers.size(); ++i) {
            auto& gltfSampler = gltf.samplers[i];
            rawModel.samplerInfos[i].maxLod = VK_LOD_CLAMP_NONE;
            rawModel.samplerInfos[i].minLod = 0;
            rawModel.samplerInfos[i].magFilter = ExtractFilter(gltfSampler.magFilter.value_or(fastgltf::Filter::Nearest));
            rawModel.samplerInfos[i].minFilter = ExtractFilter(gltfSampler.minFilter.value_or(fastgltf::Filter::Nearest));
            rawModel.samplerInfos[i].mipmapMode = ExtractMipmapMode(gltfSampler.minFilter.value_or(fastgltf::Filter::Linear));
        }
    }

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);


    if (!gltf.images.empty()) {
        rawModel.images = Core::HeapArray<RawImage>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.images.size());

        // MEM: stbi does allocs with malloc/free. I cba to use its custom macro overrides
        unsigned char* stbiData = nullptr;
        int32_t width;
        int32_t height;
        int32_t nrChannels;
        bool bFailedImageLoads = false;
        Core::Path parentPath = gltfPath.Parent();
        for (int i = 0; i < gltf.images.size(); ++i) {
            const auto& gltfImage = gltf.images[i];
            std::visit(
                fastgltf::visitor{
                    [&](auto& arg) {
                        LOG_INFO(Asset, "Whatever4");
                    },
                    [&](const fastgltf::sources::URI& fileName) {
                        if (fileName.fileByteOffset != 0) {
                            LOG_ERROR(Asset, "File byte offset is not currently supported.");
                            return;
                        }
                        if (!fileName.uri.isLocalPath()) {
                            LOG_ERROR(Asset, "Loading non-local files is not currently supported.");
                            return;
                        }

                        const Core::Path fullPath = parentPath / fileName.uri.path();
                        LOG_INFO(Asset, "Whatever");
                        stbiData = stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 4);
                    },
                    [&](const fastgltf::sources::Array& vector) {
                        if (vector.bytes.size() > 30) {
                            std::string_view strData(reinterpret_cast<const char*>(vector.bytes.data()), std::min(size_t(100), vector.bytes.size()));
                            if (strData.find("https://git-lfs.github.com/spec") != std::string_view::npos) {
                                LOG_ERROR(Asset, "Git LFS pointer detected. Run `git lfs pull` to retrieve files.");
                                return;
                            }
                        }
                        LOG_INFO(Asset, "Whatever2");
                        stbiData = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(vector.bytes.data()), static_cast<int>(vector.bytes.size()), &width, &height, &nrChannels, 4);
                    },
                    [&](const fastgltf::sources::BufferView& view) {
                        const fastgltf::BufferView& bufferView = gltf.bufferViews[view.bufferViewIndex];
                        const fastgltf::Buffer& buffer = gltf.buffers[bufferView.bufferIndex];
                        LOG_INFO(Asset, "Whatever3");
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

            if (!stbiData) {
                bFailedImageLoads = true;
                break;
            }

            rawModel.images[i].w = width;
            rawModel.images[i].h = height;
            rawModel.images[i].bpp = 4;
            size_t size = width * height * 4;
            rawModel.images[i].data = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, size);
            memcpy(rawModel.images[i].data.Data(), stbiData, size);
            stbi_image_free(stbiData);
            stbiData = nullptr;
        }

        if (bFailedImageLoads) {
            rawModel.images = {};
            SPDLOG_ERROR("Mismatch of loaded images and expected images in the gltf");
            return false;
        }
    }

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);

    // Materials
    if (!gltf.materials.empty()) {
        rawModel.materials = Core::HeapArray<Engine::Material>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.materials.size());
        for (int32_t i = 0; i < gltf.materials.size(); ++i) {
            char indexBuf[16];
            const auto [ptr, ec] = std::to_chars(indexBuf, indexBuf + sizeof(indexBuf), i);
            *ptr = '\0';

            rawModel.materials[i].name = rawModel.name;
            rawModel.materials[i].name.Append("_material_");
            rawModel.materials[i].name.Append(indexBuf);
            // mat.id             not relevant for model-based materials
            // mat.sourcePath     not relevant for model-based materials
            // mat.pipelineID = ; not yet used, but will likely just point to the ID of the "generic lit shader"
            rawModel.materials[i].immutable = true;
            rawModel.materials[i].props = ExtractMaterial(gltf, gltf.materials[i]);
        }
    }

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Meshes
    if (!gltf.meshes.empty()) {
        rawModel.allMeshes = Core::HeapArray<Engine::MeshInformation>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.meshes.size());

        size_t totalPrimitives = 0;
        for (fastgltf::Mesh& mesh : gltf.meshes) {
            totalPrimitives += mesh.primitives.size();
        }

        rawModel.primitives = Core::HeapArray<Primitive>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, totalPrimitives);
        int32_t currentPrimitiveIndex = -1;
        for (int32_t meshIndex = 0; meshIndex < gltf.meshes.size(); ++meshIndex) {
            fastgltf::Mesh& mesh = gltf.meshes[meshIndex];

            Engine::MeshInformation& meshOutput = rawModel.allMeshes[meshIndex];
            meshOutput.name = Core::InlineString<64>(mesh.name.c_str());

            for (fastgltf::Primitive& p : mesh.primitives) {
                currentPrimitiveIndex++;
                Primitive& primitiveData = rawModel.primitives[currentPrimitiveIndex];

                int32_t materialIndex{-1};
                Core::HeapArray<uint32_t> primitiveIndices;
                Core::HeapArray<Vertex> primitiveVertices;

                // Extract accessor data
                {
                    if (p.materialIndex.has_value()) {
                        materialIndex = static_cast<int32_t>(p.materialIndex.value());
                        primitiveData.bHasTransparent = (static_cast<Engine::MaterialType>(rawModel.materials[materialIndex].props.alphaProperties.y) == Engine::MaterialType::BLEND);
                    }

                    // INDICES
                    const fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
                    primitiveIndices = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, indexAccessor.count);
                    fastgltf::iterateAccessorWithIndex<uint32_t>(gltf, indexAccessor, [&](const uint32_t idx, const size_t index) {
                        primitiveIndices[index] = idx;
                    });

                    // POSITION (REQUIRED)
                    const fastgltf::Attribute* positionIt = p.findAttribute("POSITION");
                    const fastgltf::Accessor& posAccessor = gltf.accessors[positionIt->accessorIndex];
                    primitiveVertices = Core::HeapArray<Vertex>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, posAccessor.count);
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

                    // todo: Skinned Rendering will be done in another model format
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
                                    float u = std::max(static_cast<float>(uv.x()) / 32767.0f, -1.0f);
                                    float v = std::max(static_cast<float>(uv.y()) / 32767.0f, -1.0f);
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

                assert(primitiveIndices.IsAllocated());
                assert(primitiveVertices.IsAllocated());
                // Optimize Vertex and Index Buffer.
                {
                    ZoneScopedN("Optimize Mesh");


                    size_t indexCount = primitiveIndices.Size();
                    size_t vertexCount = primitiveVertices.Size();
                    auto remap = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, vertexCount);

                    size_t uniqueVertices;
                    //
                    {
                        ZoneScopedN("Generate Vertex Remap");
                        uniqueVertices = meshopt_generateVertexRemap(
                            remap.Data(),
                            primitiveIndices.Data(),
                            indexCount,
                            primitiveVertices.Data(),
                            vertexCount,
                            sizeof(Vertex));
                    }
                    //
                    {
                        ZoneScopedN("Remap Buffers");
                        auto remappedIndices = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, primitiveIndices.Size());;
                        meshopt_remapIndexBuffer(
                            remappedIndices.Data(),
                            primitiveIndices.Data(),
                            primitiveIndices.Size(),
                            remap.Data());

                        auto remappedVertices = Core::HeapArray<Vertex>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, uniqueVertices);
                        meshopt_remapVertexBuffer(
                            remappedVertices.Data(),
                            primitiveVertices.Data(),
                            primitiveVertices.Size(),
                            sizeof(Vertex),
                            remap.Data()
                        );

                        primitiveIndices = std::move(remappedIndices);
                        primitiveVertices = std::move(remappedVertices);
                    }
                    //
                    {
                        ZoneScopedN("Optimize Vertex Cache");
                        meshopt_optimizeVertexCache(
                            primitiveIndices.Data(),
                            primitiveIndices.Data(),
                            primitiveIndices.Size(),
                            primitiveVertices.Size()
                        );
                    }
                    //
                    {
                        ZoneScopedN("Optimize Overdraw");
                        meshopt_optimizeOverdraw(
                            primitiveIndices.Data(),
                            primitiveIndices.Data(),
                            primitiveIndices.Size(),
                            &primitiveVertices[0].position.x,
                            primitiveVertices.Size(),
                            sizeof(Vertex),
                            1.05f
                        );
                    }
                    //
                    {
                        ZoneScopedN("Optimize Vertex Fetch");
                        meshopt_optimizeVertexFetch(
                            primitiveVertices.Data(),
                            primitiveIndices.Data(),
                            primitiveIndices.Size(),
                            primitiveVertices.Data(),
                            primitiveVertices.Size(),
                            sizeof(Vertex)
                        );
                    }
                }

                // All LODs draw from the same "source vertex buffer".
                // Perhaps with LOD streaming this needs to change
                uint32_t vertexOffset = rawModel.vertices.Size();
                rawModel.vertices.Reserve(rawModel.vertices.Size() + primitiveVertices.Size());
                for (const auto& v : primitiveVertices) {
                    rawModel.vertices.PushBack(v);
                }

                // todo: prepare LODs for this too? Determine when using for raytracing.
                // There are concerns this won't line up with meshlet geometry
                primitiveData.indexOffset = static_cast<uint32_t>(rawModel.indices.Size());
                rawModel.indices.Reserve(rawModel.indices.Size() + primitiveIndices.Size());
                for (uint32_t idx : primitiveIndices) {
                    rawModel.indices.PushBack(idx + vertexOffset);
                }

                Core::Array<Core::HeapArray<uint32_t>, LOD_COUNT> lodIndices{};
                Core::Array<Core::HeapArray<meshopt_Meshlet>, LOD_COUNT> lodMeshlets{};
                Core::Array<Core::HeapArray<uint32_t>, LOD_COUNT> lodMeshletVertices{};
                Core::Array<Core::HeapArray<uint8_t>, LOD_COUNT> lodMeshletTriangles{};

                struct LodInformation
                {
                    int32_t indexCount;
                    int32_t meshletCount;
                    int32_t meshletVertexCount;
                    int32_t meshletTriangleCount;
                };
                Core::Array<LodInformation, LOD_COUNT> lodInformation;

                lodIndices[0] = std::move(primitiveIndices);
                lodInformation[0].indexCount = lodIndices[0].Size();

                //
                {
                    for (uint32_t lod = 1; lod < LOD_COUNT; ++lod) {
                        ZoneScopedN("Generate LOD");

                        constexpr float thresholds[LOD_COUNT - 1]{0.5f, 0.5f, 0.3f};
                        size_t targetIndexCount = lodInformation[lod - 1].indexCount * thresholds[lod - 1];
                        targetIndexCount = targetIndexCount / 3 * 3;

                        // Allocate worst case (equal to previous LOD)
                        lodIndices[lod] = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, lodIndices[lod - 1].Size());

                        lodInformation[lod].indexCount = meshopt_simplify(
                            lodIndices[lod].Data(),
                            lodIndices[lod - 1].Data(),
                            lodInformation[lod - 1].indexCount,
                            &primitiveVertices[0].position.x,
                            primitiveVertices.Size(),
                            sizeof(Vertex),
                            targetIndexCount,
                            0.01f
                        );
                    }

                    for (size_t lod = 0; lod < LOD_COUNT; ++lod) {
                        ZoneScopedN("Build Meshlets LOD");

                        size_t maxMeshlets = meshopt_buildMeshletsBound(
                            lodInformation[lod].indexCount,
                            MESHLET_MAX_VERTICES,
                            MESHLET_MAX_TRIANGLES
                        );

                        lodMeshlets[lod] = Core::HeapArray<meshopt_Meshlet>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxMeshlets);
                        lodMeshletVertices[lod] = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxMeshlets * MESHLET_MAX_VERTICES);
                        lodMeshletTriangles[lod] = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxMeshlets * MESHLET_MAX_TRIANGLES * 3);

                        size_t meshletCount = meshopt_buildMeshlets(
                            lodMeshlets[lod].Data(),
                            lodMeshletVertices[lod].Data(),
                            lodMeshletTriangles[lod].Data(),
                            lodIndices[lod].Data(),
                            lodInformation[lod].indexCount,
                            &primitiveVertices[0].position.x,
                            primitiveVertices.Size(),
                            sizeof(Vertex),
                            MESHLET_MAX_VERTICES,
                            MESHLET_MAX_TRIANGLES,
                            0.0f
                        );

                        lodInformation[lod].meshletCount = meshletCount;

                        // Optimize each meshlet
                        {
                            ZoneScopedN("Optimize Meshlets");
                            for (int32_t m = 0; m < lodInformation[lod].meshletCount; ++m) {
                                auto& meshlet = lodMeshlets[lod][m];
                                meshopt_optimizeMeshlet(
                                    &lodMeshletVertices[lod][meshlet.vertex_offset],
                                    &lodMeshletTriangles[lod][meshlet.triangle_offset],
                                    meshlet.triangle_count,
                                    meshlet.vertex_count
                                );
                            }
                        }

                        // Trim
                        const meshopt_Meshlet& last = lodMeshlets[lod][meshletCount - 1];
                        lodInformation[lod].meshletVertexCount = last.vertex_offset + last.vertex_count;
                        lodInformation[lod].meshletTriangleCount = last.triangle_offset + last.triangle_count * 3;
                    }
                }

                primitiveData.boundingSphere = GenerateBoundingSphere(primitiveVertices);

                for (size_t lod = 0; lod < LOD_COUNT; ++lod) {
                    primitiveData.meshletOffset[lod] = static_cast<int32_t>(rawModel.meshlets.Size());
                    primitiveData.meshletCount[lod] = lodInformation[lod].meshletCount;

                    uint32_t meshletVertexOffset = rawModel.meshletVertices.Size();
                    uint32_t meshletTrianglesOffset = rawModel.meshletTriangles.Size();

                    rawModel.meshletVertices.Reserve(rawModel.meshletVertices.Size() + lodInformation[lod].meshletVertexCount);
                    for (int i = 0; i < lodInformation[lod].meshletVertexCount; ++i) {
                        rawModel.meshletVertices.PushBack(lodMeshletVertices[lod][i]);
                    }

                    rawModel.meshletTriangles.Reserve(rawModel.meshletTriangles.Size() + lodInformation[lod].meshletTriangleCount);
                    for (int i = 0; i < lodInformation[lod].meshletTriangleCount; ++i) {
                        rawModel.meshletTriangles.PushBack(lodMeshletTriangles[lod][i]);
                    }

                    //
                    {
                        ZoneScopedN("ComputeMeshletBounds");
                        for (int32_t m = 0; m < lodInformation[lod].meshletCount; ++m) {
                            auto& meshlet = lodMeshlets[lod][m];
                            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                                &lodMeshletVertices[lod][meshlet.vertex_offset],
                                &lodMeshletTriangles[lod][meshlet.triangle_offset],
                                meshlet.triangle_count,
                                reinterpret_cast<const float*>(primitiveVertices.Data()),
                                primitiveVertices.Size(),
                                sizeof(Vertex)
                            );

                            rawModel.meshlets.PushBack({
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

                meshOutput.primitiveProperties.PushBack({static_cast<uint32_t>(currentPrimitiveIndex), materialIndex});
            }
        }
    }

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Nodes
    rawModel.nodes = Core::HeapArray<Engine::Node>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.nodes.size());
    for (int32_t i = 0; i < gltf.nodes.size(); ++i) {
        auto& node = gltf.nodes[i];

        Engine::Node& outputNode = rawModel.nodes[i];
        outputNode.name = Core::InlineString<64>(node.name.c_str());

        if (node.meshIndex.has_value()) {
            outputNode.meshIndex = static_cast<int>(*node.meshIndex);
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

                    outputNode.localTranslation = glm::vec3(glmMatrix[3]);
                    outputNode.localRotation = glm::quat_cast(glmMatrix);
                    outputNode.localScale = glm::vec3(
                        glm::length(glm::vec3(glmMatrix[0])),
                        glm::length(glm::vec3(glmMatrix[1])),
                        glm::length(glm::vec3(glmMatrix[2]))
                    );
                },
                [&](fastgltf::TRS transform) {
                    outputNode.localTranslation = {transform.translation[0], transform.translation[1], transform.translation[2]};
                    outputNode.localRotation = {transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]};
                    outputNode.localScale = {transform.scale[0], transform.scale[1], transform.scale[2]};
                }
            }
            , node.transform
        );
    }

    for (int i = 0; i < gltf.nodes.size(); i++) {
        for (size_t& child : gltf.nodes[i].children) {
            rawModel.nodes[child].parent = i;
        }
    }
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Skins - Will move to skinned mesh generate
    // Only import first skin
    /*if (!gltf.skins.empty()) {
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
    }*/
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Node processing
    auto nodeRemap = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.nodes.size());
    TopologicalSortNodes(rawModel.nodes, nodeRemap);
    for (size_t i = 0; i < rawModel.nodes.Size(); ++i) {
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

    // Animations - Will move to skinned mesh generate
    /*rawModel.animations.reserve(gltf.animations.size());
    for (fastgltf::Animation& gltfAnim : gltf.animations) {
        Engine::Animation anim{};
        anim.name = gltfAnim.name;

        for (fastgltf::AnimationSampler& animSampler : gltfAnim.samplers) {
            Engine::AnimationSampler sampler;

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
                    sampler.interpolation = Engine::AnimationSampler::Interpolation::Linear;
                    break;
                case fastgltf::AnimationInterpolation::Step:
                    sampler.interpolation = Engine::AnimationSampler::Interpolation::Step;
                    break;
                case fastgltf::AnimationInterpolation::CubicSpline:
                    sampler.interpolation = Engine::AnimationSampler::Interpolation::CubicSpline;
                    break;
            }

            anim.samplers.push_back(std::move(sampler));
        }

        anim.channels.reserve(gltfAnim.channels.size());
        for (auto& gltfChannel : gltfAnim.channels) {
            Engine::AnimationChannel channel{};
            channel.samplerIndex = gltfChannel.samplerIndex;
            channel.targetNodeIndex = nodeRemap[gltfChannel.nodeIndex.value()];

            switch (gltfChannel.path) {
                case fastgltf::AnimationPath::Translation:
                    channel.targetPath = Engine::AnimationChannel::TargetPath::Translation;
                    break;
                case fastgltf::AnimationPath::Rotation:
                    channel.targetPath = Engine::AnimationChannel::TargetPath::Rotation;
                    break;
                case fastgltf::AnimationPath::Scale:
                    channel.targetPath = Engine::AnimationChannel::TargetPath::Scale;
                    break;
                case fastgltf::AnimationPath::Weights:
                    channel.targetPath = Engine::AnimationChannel::TargetPath::Weights;
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
    }*/
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);
    return true;
}

bool StaticModelGenerateSlot::WriteStaticModel()
{
    ZoneScopedN("WriteStaticModel");

    std::vector<DXGI_FORMAT> preferredImageFormats;
    preferredImageFormats.resize(rawModel.images.Size(), DXGI_FORMAT_BC7_UNORM);

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
    textureIDs.reserve(rawModel.images.Size());

    for (int32_t i = 0; i < static_cast<int32_t>(rawModel.images.Size()); ++i) {
        RawImage& image = rawModel.images[i];
        Core::Path textureOutPath = gltfPath.Parent() / "textures" / (std::string(gltfPath.Stem()) + "_texture_" + std::to_string(i) + ".wtexture");
        textureIDs.push_back(generator->RequestTextureGenerateFromMemory(std::move(image.data), image.w, image.h, image.bpp, textureOutPath, true, preferredImageFormats[i]));
    }

    auto texRef = [&](int idx) -> Engine::TextureID {
        return idx >= 0 && idx < static_cast<int>(textureIDs.size()) ? textureIDs[idx] : Engine::TextureID::INVALID;
    };
    auto sampDesc = [&](int idx) -> Engine::SamplerDesc {
        return idx >= 0 && idx < static_cast<int>(rawModel.samplerInfos.Size()) ? rawModel.samplerInfos[idx] : Engine::SamplerDesc{};
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


    // Write Output
    {
        ZoneScopedN("WriteStaticModelFile");
        std::ofstream file(outputPath.c_str(), std::ios::binary);
        Engine::WStaticModelHeader header{};
        header.modelId = modelId;
        const size_t copyLen = std::min(rawModel.name.Size(), Engine::WSTATICMODEL_NAME_LENGTH - 1);
        memcpy(header.name, rawModel.name.c_str(), copyLen);
        header.name[copyLen] = '\0';

        uint32_t meshNodeCount = 0;
        for (const auto& node : rawModel.nodes) {
            if (node.meshIndex != ~0u) { ++meshNodeCount; }
        }
        header.nodeCount = static_cast<uint32_t>(rawModel.nodes.Size());
        header.meshNodeCount = meshNodeCount;

        std::vector<std::byte> body;

        header.vertexOffset = static_cast<uint32_t>(body.size());
        header.vertexCount = static_cast<uint32_t>(rawModel.vertices.Size());
        Engine::WriteVector(body, rawModel.vertices);

        header.indexOffset = static_cast<uint32_t>(body.size());
        header.indexCount = static_cast<uint32_t>(rawModel.indices.Size());
        Engine::WriteVector(body, rawModel.indices);

        header.meshletVertexOffset = static_cast<uint32_t>(body.size());
        header.meshletVertexCount = static_cast<uint32_t>(rawModel.meshletVertices.Size());
        Engine::WriteVector(body, rawModel.meshletVertices);

        header.meshletTriangleOffset = static_cast<uint32_t>(body.size());
        header.meshletTriangleCount = static_cast<uint32_t>(rawModel.meshletTriangles.Size());
        Engine::WriteVector(body, rawModel.meshletTriangles);

        header.meshletOffset = static_cast<uint32_t>(body.size());
        header.meshletCount = static_cast<uint32_t>(rawModel.meshlets.Size());
        Engine::WriteVector(body, rawModel.meshlets);

        header.primitiveOffset = static_cast<uint32_t>(body.size());
        header.primitiveCount = static_cast<uint32_t>(rawModel.primitives.Size());
        Engine::WriteArray(body, rawModel.primitives);

        header.materialOffset = static_cast<uint32_t>(body.size());
        header.materialCount = static_cast<uint32_t>(rawModel.materials.Size());
        for (const auto& mat : rawModel.materials) {
            Engine::WriteMaterial(body, mat);
        }

        header.meshOffset = static_cast<uint32_t>(body.size());
        header.meshCount = static_cast<uint32_t>(rawModel.allMeshes.Size());
        for (const auto& mesh : rawModel.allMeshes) {
            Engine::WriteMeshInformation(body, mesh);
        }

        Engine::ModelBounds bounds{};

        //
        {
            std::vector<glm::vec3> positions;
            positions.reserve(rawModel.vertices.Size());
            for (const auto& v : rawModel.vertices) { positions.push_back(v.position); }
            std::vector indices(rawModel.indices.begin(), rawModel.indices.end());
            bounds = Engine::StaticModel::ComputeBounds(positions, &indices);
        }

        std::vector<uint8_t> compressedBody = Engine::CompressLZ4(body.data(), body.size());
        header.compressedBodySize = compressedBody.size();
        header.uncompressedBodySize = body.size();

        std::vector<std::byte> nodeSection;
        for (const auto& node : rawModel.nodes) {
            Engine::WriteNode(nodeSection, node);
        }

        Engine::WriteWStaticModelHeader(file, header);
        file.write(reinterpret_cast<const char*>(compressedBody.data()), static_cast<std::streamsize>(compressedBody.size()));
        file.write(reinterpret_cast<const char*>(nodeSection.data()), static_cast<std::streamsize>(nodeSection.size()));
        file.write(reinterpret_cast<const char*>(&bounds), sizeof(Engine::ModelBounds));
    }

    progress->value.store(100, std::memory_order_release);
    return true;
}


VkFilter StaticModelGenerateSlot::ExtractFilter(fastgltf::Filter filter)
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

VkSamplerMipmapMode StaticModelGenerateSlot::ExtractMipmapMode(fastgltf::Filter filter)
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

MaterialProperties StaticModelGenerateSlot::ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial)
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
            material.alphaProperties.y = static_cast<float>(Engine::MaterialType::SOLID);
            break;
        case fastgltf::AlphaMode::Blend:
            material.alphaProperties.y = static_cast<float>(Engine::MaterialType::BLEND);
            break;
        case fastgltf::AlphaMode::Mask:
            material.alphaProperties.y = static_cast<float>(Engine::MaterialType::CUTOUT);
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

void StaticModelGenerateSlot::LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform)
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

Vec4 StaticModelGenerateSlot::GenerateBoundingSphere(Core::Span<Vertex> vertices)
{
    glm::vec3 center = {0, 0, 0};

    for (const auto& vertex : vertices) {
        center += vertex.position;
    }
    center /= static_cast<float>(vertices.Size());


    float radius = glm::dot(vertices[0].position - center, vertices[0].position - center);
    for (size_t i = 1; i < vertices.Size(); ++i) {
        radius = std::max(radius, glm::dot(vertices[i].position - center, vertices[i].position - center));
    }
    radius = std::nextafter(sqrtf(radius), std::numeric_limits<float>::max());

    return {center, radius};
}

void StaticModelGenerateSlot::TopologicalSortNodes(Core::Span<Engine::Node> nodes, Core::Span<uint32_t> oldToNew)
{
    assert(oldToNew.Size() == nodes.Size());

    auto sortedNodes = Core::FixedVector<Engine::Node>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, nodes.Size());
    auto visited = Core::HeapArray<bool>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, nodes.Size());
    for (bool& v : visited) {
        v = false;
    }

    // Topological sort
    Core::InlineFunction<void(uint32_t)> visit = [&](uint32_t idx) {
        if (visited[idx]) { return; }
        visited[idx] = true;

        if (nodes[idx].parent != ~0u) {
            visit(nodes[idx].parent);
        }

        oldToNew[idx] = sortedNodes.Size();
        sortedNodes.PushBack(nodes[idx]);
    };

    for (uint32_t i = 0; i < nodes.Size(); ++i) {
        visit(i);
    }

    for (auto& node : sortedNodes) {
        if (node.parent != ~0u) {
            node.parent = oldToNew[node.parent];
        }
    }

    for (int i = 0; i < nodes.Size(); ++i) {
        nodes[i] = sortedNodes[i];
    }
}
} // Render
