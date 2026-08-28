//
// Created by William on 2026-02-02.
//

#include "static_model_generate_slot.h"

#include <cstring>

#include <cgltf/cgltf.h>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <meshoptimizer/src/meshoptimizer.h>

#include "asset_generator.h"
#include "core/containers/fixed_map.h"
#include "core/containers/fixed_vector.h"
#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/model/model_format.h"
#include "engine/resources/texture/texture_format.h"
#include "engine/serialization/serialization.h"
#include "asset-load/asset_load_utils.h"
#include "render/render_utils.h"
#include "render/shaders/constants_interop.h"
#include "platform/file_utils.h"
#include "tracy/Tracy.hpp"

namespace Editor
{
/**
 * A BLAS build needs roughly 200 bytes of scratch per triangle, so this keeps every primitive under the loader's fixed per-slot scratch buffer (AssetLoad::BLAS_SCRATCH_SLOT_SIZE) with margin for driver variance.
 */
static constexpr uint32_t BLAS_SPLIT_TRIANGLE_TARGET = 16384;

static uint32_t ComputeBlasSplitCount(uint32_t triangleCount, uint32_t remainingPrimitiveSlots)
{
    const uint32_t desired = (triangleCount + BLAS_SPLIT_TRIANGLE_TARGET - 1) / BLAS_SPLIT_TRIANGLE_TARGET;
    return std::clamp(desired, 1u, std::max(remainingPrimitiveSlots, 1u));
}

struct GltfParseArena
{
    uint8_t* base{nullptr};
    size_t capacity{0};
    size_t used{0};
    Core::TlsfAllocator* fallback{nullptr};
};

static void* GltfArenaAlloc(void* user, cgltf_size size)
{
    auto* arena = static_cast<GltfParseArena*>(user);
    const size_t offset = (arena->used + 15) & ~size_t(15);
    if (offset + size <= arena->capacity) {
        arena->used = offset + size;
        return arena->base + offset;
    }
    return arena->fallback->Alloc(size, Core::AllocTag::AssetGenerator);
}

static void GltfArenaFree(void* user, void* ptr)
{
    if (ptr == nullptr) { return; }
    auto* arena = static_cast<GltfParseArena*>(user);
    const auto* p = static_cast<const uint8_t*>(ptr);
    if (p >= arena->base && p < arena->base + arena->capacity) { return; }
    arena->fallback->Free(ptr);
}

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

void StaticModelGenerateSlot::Launch(ModelGenerateSlotHandle _slotHandle, const Core::Path& _gltfPath, const Core::Path& _outputPath, const Core::Path& _textureOutputPath, uint64_t _modelId, uint64_t _contentVersion, bool _bSkipExistingTextures)
{
    gltfPath = Core::Path{};
    slotHandle = _slotHandle;
    gltfPath = _gltfPath;
    outputPath = _outputPath;
    textureOutputPath = _textureOutputPath;
    modelId = _modelId;
    contentVersion = _contentVersion;
    bSkipExistingTextures = _bSkipExistingTextures;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }

    task.taskSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void StaticModelGenerateSlot::Clear()
{
    outputPath = Core::Path{};
    textureOutputPath = Core::Path{};
    rawModel.Reset();
    rawModel.Init(&memoryManager->AssetsScratch());
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

    Platform::ScopedFileMapping gltfMapping(gltfPath);
    if (gltfMapping.data == nullptr) {
        LOG_ERROR(Asset, "Failed to map glTF file: {}", gltfPath.c_str());
        return false;
    }

    uint64_t jsonSize = gltfMapping.size;
    if (gltfMapping.size >= 16 && memcmp(gltfMapping.data, "glTF", 4) == 0) {
        uint32_t jsonChunkLength = 0;
        memcpy(&jsonChunkLength, gltfMapping.data + 12, sizeof(jsonChunkLength));
        jsonSize = jsonChunkLength;
    }
    const size_t arenaBytes = static_cast<size_t>(jsonSize) * 3 + (2ull << 20);
    Core::HeapArray<uint8_t> parseArenaStorage(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, arenaBytes);
    GltfParseArena parseArena{parseArenaStorage.Data(), parseArenaStorage.Size(), 0, &memoryManager->AssetsScratch()};

    cgltf_options options{};
    options.memory.alloc_func = GltfArenaAlloc;
    options.memory.free_func = GltfArenaFree;
    options.memory.user_data = &parseArena;

    cgltf_data* gltfData = nullptr;
    const cgltf_result parseResult = cgltf_parse(&options, gltfMapping.data, gltfMapping.size, &gltfData);
    if (parseResult != cgltf_result_success) {
        LOG_ERROR(Asset, "Failed to parse glTF file ({}): cgltf result {}", gltfPath.Filename(), static_cast<int>(parseResult));
        return false;
    }

    struct GltfGuard
    {
        cgltf_data* data;
        ~GltfGuard() { cgltf_free(data); }
    } gltfGuard{gltfData};

    const cgltf_result bufferResult = cgltf_load_buffers(&options, gltfData, gltfPath.c_str());
    if (bufferResult != cgltf_result_success) {
        LOG_ERROR(Asset, "Failed to load glTF buffers ({}): cgltf result {}", gltfPath.Filename(), static_cast<int>(bufferResult));
        return false;
    }

    const cgltf_data& gltf = *gltfData;
    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);

    rawModel.name = Core::InlineString<128>(gltfPath.Filename());

    // Samplers
    if (gltf.samplers_count > 0) {
        rawModel.samplerInfos = Core::HeapArray<Engine::SamplerDesc>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.samplers_count);
        for (size_t i = 0; i < gltf.samplers_count; ++i) {
            const cgltf_sampler& gltfSampler = gltf.samplers[i];
            rawModel.samplerInfos[i].maxLod = VK_LOD_CLAMP_NONE;
            rawModel.samplerInfos[i].minLod = 0;
            rawModel.samplerInfos[i].magFilter = ExtractFilter(gltfSampler.mag_filter != cgltf_filter_type_undefined ? gltfSampler.mag_filter : cgltf_filter_type_nearest);
            rawModel.samplerInfos[i].minFilter = ExtractMinFilter(gltfSampler.min_filter != cgltf_filter_type_undefined ? gltfSampler.min_filter : cgltf_filter_type_nearest);
            rawModel.samplerInfos[i].mipmapMode = ExtractMipmapMode(gltfSampler.min_filter != cgltf_filter_type_undefined ? gltfSampler.min_filter : cgltf_filter_type_linear);
            rawModel.samplerInfos[i].addressModeU = ExtractAddressMode(gltfSampler.wrap_s);
            rawModel.samplerInfos[i].addressModeV = ExtractAddressMode(gltfSampler.wrap_t);
        }
    }

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);


    if (gltf.images_count > 0) {
        rawModel.images = Core::HeapArray<RawImage>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.images_count);

        // MEM: stbi does allocs with malloc/free. I cba to use its custom macro overrides
        unsigned char* stbiData = nullptr;
        const uint8_t* embeddedBlob = nullptr;
        size_t embeddedBlobSize = 0;
        void* base64Blob = nullptr;
        int32_t width;
        int32_t height;
        int32_t nrChannels;
        bool bSuccessfullyProcessedImage = false;
        Core::Path parentPath = gltfPath.Parent();
        for (size_t i = 0; i < gltf.images_count; ++i) {
            const cgltf_image& gltfImage = gltf.images[i];

            if (gltfImage.buffer_view != nullptr) {
                const cgltf_buffer_view& bufferView = *gltfImage.buffer_view;
                if (bufferView.buffer->data != nullptr) {
                    embeddedBlob = static_cast<const uint8_t*>(bufferView.buffer->data) + bufferView.offset;
                    embeddedBlobSize = bufferView.size;
                }
            }
            else if (gltfImage.uri != nullptr && strncmp(gltfImage.uri, "data:", 5) == 0) {
                const char* comma = strchr(gltfImage.uri, ',');
                if (comma != nullptr) {
                    const char* b64 = comma + 1;
                    const size_t b64Len = strlen(b64);
                    size_t decodedSize = b64Len / 4 * 3;
                    if (b64Len >= 2) {
                        if (b64[b64Len - 1] == '=') { decodedSize--; }
                        if (b64[b64Len - 2] == '=') { decodedSize--; }
                    }
                    if (cgltf_load_buffer_base64(&options, decodedSize, b64, &base64Blob) == cgltf_result_success) {
                        embeddedBlob = static_cast<const uint8_t*>(base64Blob);
                        embeddedBlobSize = decodedSize;
                    }
                }
            }
            else if (gltfImage.uri != nullptr) {
                if (strstr(gltfImage.uri, "://") != nullptr) {
                    LOG_ERROR(Asset, "Loading non-local files is not currently supported.");
                }
                else {
                    char uriBuf[512];
                    const size_t uriLen = std::min(strlen(gltfImage.uri), sizeof(uriBuf) - 1);
                    memcpy(uriBuf, gltfImage.uri, uriLen);
                    uriBuf[uriLen] = '\0';
                    cgltf_decode_uri(uriBuf);
                    const std::string_view uriPath{uriBuf};

                    const size_t dotPos = uriPath.rfind('.');
                    const std::string_view ext = dotPos != std::string_view::npos ? uriPath.substr(dotPos) : std::string_view{};
                    const std::string_view stem = uriPath.substr(0, dotPos != std::string_view::npos ? dotPos : uriPath.size());
                    // Skip DDS
                    const bool bForceFallback = (ext == ".dds" || ext == ".DDS");

                    Core::Path candidate = parentPath / uriPath;
                    if (!bForceFallback && candidate.Exists()) {
                        rawModel.images[i].sourcePath = Core::InlinePath<256>{uriPath};
                    }
                    else {
                        constexpr std::string_view altExts[] = {".png", ".jpg", ".jpeg", ".tga"};
                        for (const std::string_view altExt : altExts) {
                            Core::InlineString<512> altUri{stem};
                            altUri.Append(altExt);
                            if ((parentPath / altUri.c_str()).Exists()) {
                                rawModel.images[i].sourcePath = Core::InlinePath<256>{altUri.View()};
                                break;
                            }
                        }
                    }
                    stbiData = nullptr;
                }
            }

            if (embeddedBlob != nullptr && embeddedBlobSize > 30) {
                const std::string_view strData(reinterpret_cast<const char*>(embeddedBlob), std::min(size_t(100), embeddedBlobSize));
                if (strData.find("https://git-lfs.github.com/spec") != std::string_view::npos) {
                    LOG_ERROR(Asset, "Git LFS pointer detected. Run `git lfs pull` to retrieve files.");
                    embeddedBlob = nullptr;
                    embeddedBlobSize = 0;
                }
            }

            if (embeddedBlob != nullptr) {
                const bool bPng = embeddedBlobSize > 8 && embeddedBlob[0] == 0x89 && embeddedBlob[1] == 'P' && embeddedBlob[2] == 'N' && embeddedBlob[3] == 'G';
                const bool bJpg = embeddedBlobSize > 3 && embeddedBlob[0] == 0xFF && embeddedBlob[1] == 0xD8 && embeddedBlob[2] == 0xFF;
                if (bPng || bJpg) {
                    char indexBuf[16];
                    *std::to_chars(indexBuf, indexBuf + sizeof(indexBuf), i).ptr = '\0';
                    Core::InlineString<300> relName(".extracted/");
                    relName.Append(gltfPath.Stem());
                    relName.Append("_texture_");
                    relName.Append(indexBuf);
                    relName.Append(bPng ? ".png" : ".jpg");
                    if (Platform::WriteFile(parentPath / relName.c_str(), embeddedBlob, embeddedBlobSize)) {
                        rawModel.images[i].sourcePath = Core::InlinePath<256>{relName.View()};
                    }
                }
                if (rawModel.images[i].sourcePath.IsEmpty()) {
                    stbiData = stbi_load_from_memory(embeddedBlob, static_cast<int>(embeddedBlobSize), &width, &height, &nrChannels, 4);
                }
                embeddedBlob = nullptr;
                embeddedBlobSize = 0;
            }

            if (base64Blob != nullptr) {
                options.memory.free_func(options.memory.user_data, base64Blob);
                base64Blob = nullptr;
            }

            if (stbiData) {
                rawModel.images[i].w = width;
                rawModel.images[i].h = height;
                rawModel.images[i].bpp = 4;
                size_t size = width * height * 4;
                rawModel.images[i].data = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, size);
                memcpy(rawModel.images[i].data.Data(), stbiData, size);
                stbi_image_free(stbiData);
                stbiData = nullptr;
                bSuccessfullyProcessedImage = true;
            }
            else if (!rawModel.images[i].sourcePath.IsEmpty()) {
                bSuccessfullyProcessedImage = true;
            }

            if (!bSuccessfullyProcessedImage) {
                rawModel.images = {};
                SPDLOG_ERROR("Mismatch of loaded images and expected images in the gltf");
                return false;
            }
        }

        if (!bSuccessfullyProcessedImage) {
            rawModel.images = {};
            SPDLOG_ERROR("Mismatch of loaded images and expected images in the gltf");
            return false;
        }
    }

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order_release);

    // Materials
    if (gltf.materials_count > 0) {
        rawModel.materials = Core::HeapArray<Engine::Material>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.materials_count);
        for (int32_t i = 0; i < static_cast<int32_t>(gltf.materials_count); ++i) {
            char indexBuf[16];
            const auto [ptr, ec] = std::to_chars(indexBuf, indexBuf + sizeof(indexBuf), i);
            *ptr = '\0';

            rawModel.materials[i] = {};
            rawModel.materials[i].name = rawModel.name;
            rawModel.materials[i].name.Append("_material_");
            rawModel.materials[i].name.Append(indexBuf);
            // mat.id             not relevant for model-based materials
            // mat.sourcePath     not relevant for model-based materials
            // mat.pipelineID = ; not yet used, but will likely just point to the ID of the "generic lit shader"
            rawModel.materials[i].bSynthesized = true;
            rawModel.materials[i].props = ExtractMaterial(gltf, gltf.materials[i]);
        }
    }

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    Core::Vector<Vec3> allPositions(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, 0);
    // Meshes
    if (gltf.meshes_count > 0) {
        rawModel.allMeshes = Core::HeapArray<Engine::MeshInformation>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.meshes_count);

        size_t totalPrimitives = 0;
        for (size_t m = 0; m < gltf.meshes_count; ++m) {
            const cgltf_mesh& mesh = gltf.meshes[m];
            uint32_t meshPrimitives = 0;
            for (size_t primIndex = 0; primIndex < mesh.primitives_count; ++primIndex) {
                const uint32_t triangleCount = static_cast<uint32_t>(mesh.primitives[primIndex].indices->count / 3);
                const uint32_t remaining = meshPrimitives < Engine::MAX_PRIMITIVES_PER_MESH ? Engine::MAX_PRIMITIVES_PER_MESH - meshPrimitives : 0;
                const uint32_t splitCount = ComputeBlasSplitCount(triangleCount, remaining);
                meshPrimitives += splitCount;
                totalPrimitives += splitCount;
            }
        }

        rawModel.primitives = Core::HeapArray<Primitive>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, totalPrimitives);
        int32_t currentPrimitiveIndex = -1;
        for (size_t meshIndex = 0; meshIndex < gltf.meshes_count; ++meshIndex) {
            const cgltf_mesh& mesh = gltf.meshes[meshIndex];

            Engine::MeshInformation& meshOutput = rawModel.allMeshes[meshIndex];
            meshOutput.name = Core::InlineString<64>(mesh.name != nullptr ? mesh.name : "");

            for (size_t primIndex = 0; primIndex < mesh.primitives_count; ++primIndex) {
                const cgltf_primitive& p = mesh.primitives[primIndex];

                int32_t materialIndex{-1};
                bool bHasTransparent = false;
                Core::HeapArray<uint32_t> sourceIndices;
                Core::HeapArray<Engine::FullVertex> sourceVertices;

                // Extract accessor data
                {
                    if (p.material != nullptr) {
                        materialIndex = static_cast<int32_t>(p.material - gltf.materials);
                        bHasTransparent = (static_cast<Engine::MaterialType>(rawModel.materials[materialIndex].props.alphaProperties.y) == Engine::MaterialType::BLEND);
                    }

                    // INDICES
                    const cgltf_accessor& indexAccessor = *p.indices;
                    sourceIndices = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, indexAccessor.count);
                    cgltf_accessor_unpack_indices(&indexAccessor, sourceIndices.Data(), sizeof(uint32_t), indexAccessor.count);

                    const cgltf_accessor* posAccessor = nullptr;
                    const cgltf_accessor* normalAccessor = nullptr;
                    const cgltf_accessor* tangentAccessor = nullptr;
                    const cgltf_accessor* uvAccessor = nullptr;
                    const cgltf_accessor* colorAccessor = nullptr;
                    for (size_t a = 0; a < p.attributes_count; ++a) {
                        const cgltf_attribute& attr = p.attributes[a];
                        switch (attr.type) {
                            case cgltf_attribute_type_position: posAccessor = attr.data;
                                break;
                            case cgltf_attribute_type_normal: normalAccessor = attr.data;
                                break;
                            case cgltf_attribute_type_tangent: tangentAccessor = attr.data;
                                break;
                            case cgltf_attribute_type_texcoord: if (attr.index == 0) { uvAccessor = attr.data; }
                                break;
                            case cgltf_attribute_type_color: if (attr.index == 0) { colorAccessor = attr.data; }
                                break;
                            default: break;
                        }
                    }

                    // POSITION (REQUIRED)
                    sourceVertices = Core::HeapArray<Engine::FullVertex>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, posAccessor->count);
                    Core::HeapArray<float> attrScratch(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, posAccessor->count * 4);

                    cgltf_accessor_unpack_floats(posAccessor, attrScratch.Data(), posAccessor->count * 3);
                    for (size_t v = 0; v < posAccessor->count; ++v) {
                        sourceVertices[v] = {};
                        sourceVertices[v].position = Vec3{attrScratch[v * 3 + 0], attrScratch[v * 3 + 1], attrScratch[v * 3 + 2]};
                        sourceVertices[v].color = {1.0f, 1.0f, 1.0f, 1.0f};
                        sourceVertices[v].normal = {0.0f, 0.0f, 1.0f};
                        sourceVertices[v].tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                    }

                    // NORMALS
                    if (normalAccessor != nullptr) {
                        cgltf_accessor_unpack_floats(normalAccessor, attrScratch.Data(), normalAccessor->count * 3);
                        for (size_t v = 0; v < normalAccessor->count; ++v) {
                            sourceVertices[v].normal = {attrScratch[v * 3 + 0], attrScratch[v * 3 + 1], attrScratch[v * 3 + 2]};
                        }
                    }

                    // TANGENTS
                    if (tangentAccessor != nullptr) {
                        cgltf_accessor_unpack_floats(tangentAccessor, attrScratch.Data(), tangentAccessor->count * 4);
                        for (size_t v = 0; v < tangentAccessor->count; ++v) {
                            sourceVertices[v].tangent = {attrScratch[v * 4 + 0], attrScratch[v * 4 + 1], attrScratch[v * 4 + 2], attrScratch[v * 4 + 3]};
                        }
                    }

                    // todo: Skinned Rendering will be done in another model format (JOINTS_0/WEIGHTS_0 in the skinned generate slot)

                    // UV (unpack_floats applies KHR_mesh_quantization normalization; the -1 snorm floor only applies to normalized accessors, raw float tiling UVs may span far below -1)
                    if (uvAccessor != nullptr) {
                        cgltf_accessor_unpack_floats(uvAccessor, attrScratch.Data(), uvAccessor->count * 2);
                        const float uvFloor = uvAccessor->normalized ? -1.0f : -FLT_MAX;
                        for (size_t v = 0; v < uvAccessor->count; ++v) {
                            sourceVertices[v].uv = {std::max(attrScratch[v * 2 + 0], uvFloor), std::max(attrScratch[v * 2 + 1], uvFloor)};
                        }
                    }

                    // VERTEX COLOR
                    if (colorAccessor != nullptr) {
                        const size_t numComponents = cgltf_num_components(colorAccessor->type);
                        if (numComponents == 3 || numComponents == 4) {
                            cgltf_accessor_unpack_floats(colorAccessor, attrScratch.Data(), colorAccessor->count * numComponents);
                            for (size_t v = 0; v < colorAccessor->count; ++v) {
                                const float alpha = numComponents == 4 ? attrScratch[v * 4 + 3] : 1.0f;
                                sourceVertices[v].color = {attrScratch[v * numComponents + 0], attrScratch[v * numComponents + 1], attrScratch[v * numComponents + 2], alpha};
                            }
                        }
                    }
                }

                assert(sourceIndices.IsAllocated());
                assert(sourceVertices.IsAllocated());

                const uint32_t sourceTriangleCount = static_cast<uint32_t>(sourceIndices.Size() / 3);
                const size_t usedPrimitiveSlots = meshOutput.primitiveProperties.Size();
                const uint32_t remainingPrimitiveSlots = usedPrimitiveSlots < Engine::MAX_PRIMITIVES_PER_MESH ? Engine::MAX_PRIMITIVES_PER_MESH - static_cast<uint32_t>(usedPrimitiveSlots) : 0;
                const uint32_t splitCount = ComputeBlasSplitCount(sourceTriangleCount, remainingPrimitiveSlots);

                if (splitCount > 1) {
                    ZoneScopedN("Spatial Sort Triangles");
                    auto sortedIndices = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, sourceIndices.Size());
                    meshopt_spatialSortTriangles(
                        sortedIndices.Data(),
                        sourceIndices.Data(),
                        sourceIndices.Size(),
                        &sourceVertices[0].position.x,
                        sourceVertices.Size(),
                        sizeof(Engine::FullVertex)
                    );
                    sourceIndices = std::move(sortedIndices);
                }

                for (uint32_t split = 0; split < splitCount; ++split) {
                    const size_t splitTriangleStart = (static_cast<size_t>(sourceTriangleCount) * split) / splitCount;
                    const size_t splitTriangleEnd = (static_cast<size_t>(sourceTriangleCount) * (split + 1)) / splitCount;

                    currentPrimitiveIndex++;
                    Primitive& primitiveData = rawModel.primitives[currentPrimitiveIndex];
                    primitiveData.bHasTransparent = bHasTransparent;

                    Core::HeapArray<uint32_t> primitiveIndices(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, (splitTriangleEnd - splitTriangleStart) * 3);
                    memcpy(primitiveIndices.Data(), sourceIndices.Data() + splitTriangleStart * 3, primitiveIndices.Size() * sizeof(uint32_t));
                    Core::HeapArray<Engine::FullVertex> primitiveVertices;

                    // Optimize Vertex and Index Buffer.
                    {
                        ZoneScopedN("Optimize Mesh");


                        size_t indexCount = primitiveIndices.Size();
                        size_t vertexCount = sourceVertices.Size();
                        auto remap = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, vertexCount);

                        size_t uniqueVertices;
                        //
                        {
                            ZoneScopedN("Generate Vertex Remap");
                            uniqueVertices = meshopt_generateVertexRemap(
                                remap.Data(),
                                primitiveIndices.Data(),
                                indexCount,
                                sourceVertices.Data(),
                                vertexCount,
                                sizeof(Engine::FullVertex));
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

                            auto remappedVertices = Core::HeapArray<Engine::FullVertex>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, uniqueVertices);
                            meshopt_remapVertexBuffer(
                                remappedVertices.Data(),
                                sourceVertices.Data(),
                                sourceVertices.Size(),
                                sizeof(Engine::FullVertex),
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
                                sizeof(Engine::FullVertex),
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
                                sizeof(Engine::FullVertex)
                            );
                        }
                    }


                    auto primitiveBounds = AssetLoad::CalculateMeshBounds(primitiveVertices);
                    primitiveData.boundingSphere = {primitiveBounds.sphere.center, primitiveBounds.sphere.radius};
                    primitiveData.boundingBoxMin = primitiveBounds.aabb.min;
                    primitiveData.boundingBoxMax = primitiveBounds.aabb.max;

                    // All LODs draw from the same "source vertex buffer".
                    // Perhaps with LOD streaming this needs to change
                    uint32_t vertexOffset = rawModel.vertices.Size();
                    rawModel.vertices.Resize(rawModel.vertices.Size() + primitiveVertices.Size());
                    CompressVertices(scheduler, primitiveVertices.Data(), static_cast<uint32_t>(primitiveVertices.Size()), primitiveBounds, rawModel.vertices.Data() + vertexOffset);
                    allPositions.Reserve(allPositions.Size() + primitiveVertices.Size());
                    for (const auto& v : primitiveVertices) {
                        allPositions.PushBack(v.position);
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
                            const int32_t prevIndexCount = lodInformation[lod - 1].indexCount;
                            size_t targetIndexCount = prevIndexCount * thresholds[lod - 1];
                            targetIndexCount = targetIndexCount / 3 * 3;

                            lodIndices[lod] = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, lodIndices[lod - 1].Size());

                            if (targetIndexCount < 3) {
                                // Too small to simplify; duplicate the previous LOD
                                memcpy(lodIndices[lod].Data(), lodIndices[lod - 1].Data(), prevIndexCount * sizeof(uint32_t));
                                lodInformation[lod].indexCount = prevIndexCount;
                            }
                            else {
                                size_t simplifiedCount = meshopt_simplify(
                                    lodIndices[lod].Data(),
                                    lodIndices[lod - 1].Data(),
                                    prevIndexCount,
                                    &primitiveVertices[0].position.x,
                                    primitiveVertices.Size(),
                                    sizeof(Engine::FullVertex),
                                    targetIndexCount,
                                    0.01f
                                );

                                if (simplifiedCount < 3) {
                                    memcpy(lodIndices[lod].Data(), lodIndices[lod - 1].Data(), prevIndexCount * sizeof(uint32_t));
                                    lodInformation[lod].indexCount = prevIndexCount;
                                }
                                else {
                                    lodInformation[lod].indexCount = simplifiedCount;
                                }
                            }
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
                                sizeof(Engine::FullVertex),
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
                                    sizeof(Engine::FullVertex)
                                );

                                rawModel.meshlets.PushBack({
                                    .meshletBoundingSphere = Vec4(
                                        bounds.center[0], bounds.center[1], bounds.center[2],
                                        bounds.radius
                                    ),
                                    .coneApex = Vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]),
                                    .coneCutoff = bounds.cone_cutoff,

                                    .coneAxis = Vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]),
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
    }


    rawModel.modelBounds = AssetLoad::ComputeBounds(allPositions);

    _progress += stepDiff;
    progress->value.store(_progress, std::memory_order::release);

    // Nodes
    rawModel.nodes = Core::HeapArray<Engine::Node>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.nodes_count);
    for (size_t i = 0; i < gltf.nodes_count; ++i) {
        const cgltf_node& node = gltf.nodes[i];

        Engine::Node& outputNode = rawModel.nodes[i];
        outputNode.name = Core::InlineString<64>(node.name != nullptr ? node.name : "");

        if (node.mesh != nullptr) {
            outputNode.meshIndex = static_cast<int>(node.mesh - gltf.meshes);
        }

        if (node.has_matrix) {
            glm::mat4 glmMatrix;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    glmMatrix[col][row] = node.matrix[col * 4 + row];
                }
            }

            outputNode.localTranslation = glm::vec3(glmMatrix[3]);
            outputNode.localRotation = glm::quat_cast(glmMatrix);
            outputNode.localScale = glm::vec3(
                glm::length(glm::vec3(glmMatrix[0])),
                glm::length(glm::vec3(glmMatrix[1])),
                glm::length(glm::vec3(glmMatrix[2]))
            );
        }
        else {
            outputNode.localTranslation = {node.translation[0], node.translation[1], node.translation[2]};
            outputNode.localRotation = {node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]};
            outputNode.localScale = {node.scale[0], node.scale[1], node.scale[2]};
        }
    }

    for (size_t i = 0; i < gltf.nodes_count; i++) {
        for (size_t c = 0; c < gltf.nodes[i].children_count; ++c) {
            rawModel.nodes[gltf.nodes[i].children[c] - gltf.nodes].parent = static_cast<uint32_t>(i);
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
    auto nodeRemap = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, gltf.nodes_count);
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

    if (!rawModel.images.IsEmpty()) {
        auto preferredImageFormats = Core::HeapArray<DXGI_FORMAT>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, rawModel.images.Size());
        for (auto& imf : preferredImageFormats) {
            imf = DXGI_FORMAT_BC7_UNORM;
        }

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


        auto textureIDs = Core::HeapArray<Engine::TextureID>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, rawModel.images.Size());

        Core::FixedMap<Core::Path, Engine::TextureID> seenTextures(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, rawModel.images.Size());

        const Core::Path texOutDir = textureOutputPath.IsEmpty() ? gltfPath.Parent() / "textures" : textureOutputPath;

        for (int32_t i = 0; i < static_cast<int32_t>(rawModel.images.Size()); ++i) {
            RawImage& image = rawModel.images[i];

            Core::Path textureOutPath;

            if (!image.sourcePath.IsEmpty()) {
                std::string_view relPath = image.sourcePath.View();
                constexpr std::string_view texPrefix = "textures/";
                if (relPath.starts_with(texPrefix)) {
                    relPath = relPath.substr(texPrefix.size());
                }
                constexpr std::string_view extractedPrefix = ".extracted/";
                if (relPath.starts_with(extractedPrefix)) {
                    relPath = relPath.substr(extractedPrefix.size());
                }

                const size_t dotPos = relPath.rfind('.');
                const std::string_view stem = relPath.substr(0, dotPos != std::string_view::npos ? dotPos : relPath.size());
                Core::InlineString<512> texName(stem);
                texName.Append(".wtexture");
                textureOutPath = texOutDir / texName.c_str();
            }
            else {
                char indexBuf[16];
                *std::to_chars(indexBuf, indexBuf + sizeof(indexBuf), i).ptr = '\0';

                Core::InlineString<128> texName(gltfPath.Stem());
                texName.Append("_texture_");
                texName.Append(indexBuf);
                texName.Append(".wtexture");
                textureOutPath = texOutDir / texName.c_str();
            }

            if (const Engine::TextureID* existing = seenTextures.Find(textureOutPath)) {
                textureIDs[i] = *existing;
                continue;
            }
            if (bSkipExistingTextures) {
                // Reuse only current-major textures; stale/missing ones still regenerate so skip mode never wires a dead ID
                if (auto header = Engine::ReadWTextureHeaderAnyVersion(textureOutPath); header && header->major == Engine::TEXTURE_MAJOR_VERSION) {
                    textureIDs[i] = Engine::TextureID{header->textureId};
                    seenTextures.Insert(textureOutPath, textureIDs[i]);
                    continue;
                }
            }
            if (image.data.IsAllocated()) {
                textureIDs[i] = generator->RequestTextureGenerateFromMemory(std::move(image.data), image.w, image.h, image.bpp, textureOutPath, true, preferredImageFormats[i], Engine::TextureCategory::Model,
                                                                            modelId, static_cast<uint32_t>(i));
                seenTextures.Insert(textureOutPath, textureIDs[i]);
            }
            else {
                const Core::Path fullSourcePath = gltfPath.Parent() / image.sourcePath.c_str();
                textureIDs[i] = generator->RequestTextureGenerateFromFile(fullSourcePath, textureOutPath, true, preferredImageFormats[i], false, Engine::TextureCategory::Model,
                                                                          modelId, static_cast<uint32_t>(i));
                seenTextures.Insert(textureOutPath, textureIDs[i]);
            }
        }

        auto texRef = [&](int idx) -> Engine::TextureID {
            return idx >= 0 && idx < static_cast<int>(textureIDs.Size()) ? textureIDs[idx] : Engine::TextureID::INVALID;
        };
        auto sampDesc = [&](int idx) -> Engine::SamplerDesc {
            return idx >= 0 && idx < static_cast<int>(rawModel.samplerInfos.Size()) ? rawModel.samplerInfos[idx] : Engine::SamplerDesc{};
        };

        if (!rawModel.materials.IsEmpty()) {
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

                // Enable anisotropy on detail slots (color, metal/rough, normal)
                for (int32_t s = 0; s < 3; ++s) {
                    if (mat.samplerDesc[s].minFilter == VK_FILTER_LINEAR) {
                        mat.samplerDesc[s].anisotropyEnable = VK_TRUE;
                        mat.samplerDesc[s].maxAnisotropy = 4.0f;
                    }
                }

                // Bindless indices are resolved at load time from textureRefs
                mat.props.textureImageIndices = glm::ivec4(-1);
                mat.props.textureSamplerIndices = glm::ivec4(-1);
                mat.props.textureImageIndices2 = glm::ivec4(-1);
                mat.props.textureSamplerIndices2 = glm::ivec4(-1);
            }
        }
    }
    else {
        if (!rawModel.materials.IsEmpty()) {
            for (auto& mat : rawModel.materials) {
                mat.textureRefs[0] = Engine::TextureID::INVALID;
                mat.textureRefs[1] = Engine::TextureID::INVALID;
                mat.textureRefs[2] = Engine::TextureID::INVALID;
                mat.textureRefs[3] = Engine::TextureID::INVALID;
                mat.textureRefs[4] = Engine::TextureID::INVALID;
                mat.textureRefs[5] = Engine::TextureID::INVALID;
                mat.samplerDesc[0] = Engine::SamplerDesc{};
                mat.samplerDesc[1] = Engine::SamplerDesc{};
                mat.samplerDesc[2] = Engine::SamplerDesc{};
                mat.samplerDesc[3] = Engine::SamplerDesc{};
                mat.samplerDesc[4] = Engine::SamplerDesc{};
                mat.samplerDesc[5] = Engine::SamplerDesc{};
            }
        }
    }


    // Write Output
    {
        ZoneScopedN("WriteStaticModelFile");
        Engine::WStaticModelHeader header{};
        header.modelId = modelId;
        header.contentVersion = contentVersion;
        const size_t copyLen = std::min(rawModel.name.Size(), Engine::WSTATICMODEL_NAME_LENGTH - 1);
        memcpy(header.name, rawModel.name.c_str(), copyLen);
        header.name[copyLen] = '\0';

        uint32_t meshNodeCount = 0;
        for (const auto& node : rawModel.nodes) {
            if (node.meshIndex != ~0u) { ++meshNodeCount; }
        }
        header.nodeCount = static_cast<uint32_t>(rawModel.nodes.Size());
        header.meshNodeCount = meshNodeCount;

        auto body = Core::Vector<std::byte>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, 0);

        header.vertexOffset = static_cast<uint32_t>(body.Size());
        header.vertexCount = static_cast<uint32_t>(rawModel.vertices.Size());
        Engine::WriteVector(body, rawModel.vertices);

        header.indexOffset = static_cast<uint32_t>(body.Size());
        header.indexCount = static_cast<uint32_t>(rawModel.indices.Size());
        Engine::WriteVector(body, rawModel.indices);

        header.meshletVertexOffset = static_cast<uint32_t>(body.Size());
        header.meshletVertexCount = static_cast<uint32_t>(rawModel.meshletVertices.Size());
        Engine::WriteVector(body, rawModel.meshletVertices);

        header.meshletTriangleOffset = static_cast<uint32_t>(body.Size());
        header.meshletTriangleCount = static_cast<uint32_t>(rawModel.meshletTriangles.Size());
        Engine::WriteVector(body, rawModel.meshletTriangles);

        header.meshletOffset = static_cast<uint32_t>(body.Size());
        header.meshletCount = static_cast<uint32_t>(rawModel.meshlets.Size());
        Engine::WriteVector(body, rawModel.meshlets);

        header.primitiveOffset = static_cast<uint32_t>(body.Size());
        header.primitiveCount = static_cast<uint32_t>(rawModel.primitives.Size());
        Engine::WriteArray(body, rawModel.primitives);

        header.materialOffset = static_cast<uint32_t>(body.Size());
        header.materialCount = static_cast<uint32_t>(rawModel.materials.Size());
        for (const auto& mat : rawModel.materials) {
            Engine::WriteMaterial(body, mat);
        }

        header.meshOffset = static_cast<uint32_t>(body.Size());
        header.meshCount = static_cast<uint32_t>(rawModel.allMeshes.Size());
        for (const auto& mesh : rawModel.allMeshes) {
            Engine::WriteMeshInformation(body, mesh);
        }

        auto maxCompressedSize = Engine::CompressMaxSize(Engine::DEFAULT_STATIC_MODEL_COMPRESSION, body.Size());
        auto compressedBody = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
        size_t realCompressedSize = Engine::Compress(Engine::DEFAULT_STATIC_MODEL_COMPRESSION, body.Data(), body.Size(), compressedBody.Data(), compressedBody.Size());

        header.compressedBodySize = realCompressedSize;
        header.uncompressedBodySize = body.Size();

        auto nodeSection = Core::Vector<std::byte>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, 0);
        for (const auto& node : rawModel.nodes) {
            Engine::WriteNode(nodeSection, node);
        }

        auto headerOut = Core::Vector<std::byte>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, 0);
        Engine::WriteWStaticModelHeader(headerOut, header);
        Engine::AppendRaw(nodeSection, &rawModel.modelBounds, sizeof(Engine::ModelBounds));
        if (!Platform::WriteFile(outputPath, headerOut.Data(), headerOut.Size()) ||
            !Platform::AppendFile(outputPath, compressedBody.Data(), realCompressedSize) ||
            !Platform::AppendFile(outputPath, nodeSection.Data(), nodeSection.Size())) {
            LOG_ERROR(Asset, "Failed to write static model file: {}", outputPath.c_str());
            return false;
        }
    }

    progress->value.store(100, std::memory_order_release);
    return true;
}


VkFilter StaticModelGenerateSlot::ExtractFilter(cgltf_filter_type filter)
{
    switch (filter) {
        // nearest samplers
        case cgltf_filter_type_nearest:
        case cgltf_filter_type_nearest_mipmap_nearest:
        case cgltf_filter_type_nearest_mipmap_linear:
            return VK_FILTER_NEAREST;
        // linear samplers
        case cgltf_filter_type_linear:
        case cgltf_filter_type_linear_mipmap_nearest:
        case cgltf_filter_type_linear_mipmap_linear:
        default:
            return VK_FILTER_LINEAR;
    }
}

VkFilter StaticModelGenerateSlot::ExtractMinFilter(cgltf_filter_type filter)
{
    if (filter == cgltf_filter_type_nearest) {
        return VK_FILTER_NEAREST;
    }
    return VK_FILTER_LINEAR;
}

VkSamplerAddressMode StaticModelGenerateSlot::ExtractAddressMode(cgltf_wrap_mode wrap)
{
    switch (wrap) {
        case cgltf_wrap_mode_clamp_to_edge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case cgltf_wrap_mode_mirrored_repeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case cgltf_wrap_mode_repeat:
        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkSamplerMipmapMode StaticModelGenerateSlot::ExtractMipmapMode(cgltf_filter_type filter)
{
    switch (filter) {
        case cgltf_filter_type_nearest:
        case cgltf_filter_type_nearest_mipmap_nearest:
        case cgltf_filter_type_linear_mipmap_nearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case cgltf_filter_type_linear:
        case cgltf_filter_type_nearest_mipmap_linear:
        case cgltf_filter_type_linear_mipmap_linear:
        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

MaterialProperties StaticModelGenerateSlot::ExtractMaterial(const cgltf_data& gltf, const cgltf_material& gltfMaterial)
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
        gltfMaterial.pbr_metallic_roughness.base_color_factor[0],
        gltfMaterial.pbr_metallic_roughness.base_color_factor[1],
        gltfMaterial.pbr_metallic_roughness.base_color_factor[2],
        gltfMaterial.pbr_metallic_roughness.base_color_factor[3]);

    material.metalRoughFactors.x = gltfMaterial.pbr_metallic_roughness.metallic_factor;
    material.metalRoughFactors.y = gltfMaterial.pbr_metallic_roughness.roughness_factor;

    if (gltfMaterial.has_pbr_specular_glossiness) {
        const cgltf_pbr_specular_glossiness& sg = gltfMaterial.pbr_specular_glossiness;
        material.colorFactor = glm::vec4(sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2], sg.diffuse_factor[3]);
        material.metalRoughFactors.x = glm::max(glm::max(sg.specular_factor[0], sg.specular_factor[1]), sg.specular_factor[2]);
        material.metalRoughFactors.y = 1.0f - sg.glossiness_factor;
    }

    material.alphaProperties.x = gltfMaterial.alpha_cutoff;
    material.alphaProperties.z = gltfMaterial.double_sided ? 1.0f : 0.0f;
    material.alphaProperties.w = gltfMaterial.unlit ? 1.0f : 0.0f;

    switch (gltfMaterial.alpha_mode) {
        case cgltf_alpha_mode_opaque:
            material.alphaProperties.y = static_cast<float>(Engine::MaterialType::SOLID);
            break;
        case cgltf_alpha_mode_blend:
            material.alphaProperties.y = static_cast<float>(Engine::MaterialType::BLEND);
            break;
        case cgltf_alpha_mode_mask:
            material.alphaProperties.y = static_cast<float>(Engine::MaterialType::CUTOUT);
            break;
        default:
            break;
    }

    material.emissiveFactor = glm::vec4(
        gltfMaterial.emissive_factor[0],
        gltfMaterial.emissive_factor[1],
        gltfMaterial.emissive_factor[2],
        gltfMaterial.has_emissive_strength ? gltfMaterial.emissive_strength.emissive_strength : 1.0f);

    material.physicalProperties.x = gltfMaterial.has_ior ? gltfMaterial.ior.ior : 1.5f;
    material.physicalProperties.y = gltfMaterial.has_dispersion ? gltfMaterial.dispersion.dispersion : 0.0f;

    // Handle edge cases for missing samplers/images
    auto fixTextureIndices = [](int& imageIdx, int& samplerIdx) {
        if (imageIdx == -1 && samplerIdx != -1) imageIdx = 0;
        if (samplerIdx == -1 && imageIdx != -1) samplerIdx = 0;
    };

    if (gltfMaterial.pbr_metallic_roughness.base_color_texture.texture != nullptr) {
        LoadTextureIndicesAndUV(gltfMaterial.pbr_metallic_roughness.base_color_texture, gltf, material.textureImageIndices.x, material.textureSamplerIndices.x, material.colorUvTransform);
        fixTextureIndices(material.textureImageIndices.x, material.textureSamplerIndices.x);
    }
    else if (gltfMaterial.has_pbr_specular_glossiness && gltfMaterial.pbr_specular_glossiness.diffuse_texture.texture != nullptr) {
        LoadTextureIndicesAndUV(gltfMaterial.pbr_specular_glossiness.diffuse_texture, gltf, material.textureImageIndices.x, material.textureSamplerIndices.x, material.colorUvTransform);
        fixTextureIndices(material.textureImageIndices.x, material.textureSamplerIndices.x);
    }

    if (gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr) {
        LoadTextureIndicesAndUV(gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture, gltf, material.textureImageIndices.y, material.textureSamplerIndices.y, material.metalRoughUvTransform);
        fixTextureIndices(material.textureImageIndices.y, material.textureSamplerIndices.y);
    }

    if (gltfMaterial.normal_texture.texture != nullptr) {
        LoadTextureIndicesAndUV(gltfMaterial.normal_texture, gltf, material.textureImageIndices.z, material.textureSamplerIndices.z, material.normalUvTransform);
        material.physicalProperties.z = gltfMaterial.normal_texture.scale;
        fixTextureIndices(material.textureImageIndices.z, material.textureSamplerIndices.z);
    }

    if (gltfMaterial.emissive_texture.texture != nullptr) {
        LoadTextureIndicesAndUV(gltfMaterial.emissive_texture, gltf, material.textureImageIndices.w, material.textureSamplerIndices.w, material.emissiveUvTransform);
        fixTextureIndices(material.textureImageIndices.w, material.textureSamplerIndices.w);
    }

    if (gltfMaterial.occlusion_texture.texture != nullptr) {
        LoadTextureIndicesAndUV(gltfMaterial.occlusion_texture, gltf, material.textureImageIndices2.x, material.textureSamplerIndices2.x, material.occlusionUvTransform);
        material.physicalProperties.w = gltfMaterial.occlusion_texture.scale;
        fixTextureIndices(material.textureImageIndices2.x, material.textureSamplerIndices2.x);
    }

    return material;
}

void StaticModelGenerateSlot::LoadTextureIndicesAndUV(const cgltf_texture_view& textureView, const cgltf_data& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform)
{
    const cgltf_texture& texture = *textureView.texture;

    if (texture.has_basisu && texture.basisu_image != nullptr) {
        imageIndex = static_cast<int>(texture.basisu_image - gltf.images);
    }
    else if (texture.image != nullptr) {
        imageIndex = static_cast<int>(texture.image - gltf.images);
    }

    if (texture.sampler != nullptr) {
        samplerIndex = static_cast<int>(texture.sampler - gltf.samplers);
    }

    if (textureView.has_transform) {
        uvTransform.x = textureView.transform.scale[0];
        uvTransform.y = textureView.transform.scale[1];
        uvTransform.z = textureView.transform.offset[0];
        uvTransform.w = textureView.transform.offset[1];
    }
}

Vec4 StaticModelGenerateSlot::GenerateBoundingSphere(Core::Span<Engine::FullVertex> vertices)
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
