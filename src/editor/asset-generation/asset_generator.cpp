//
// Created by William on 2025-12-15.
//

#include "asset_generator.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <ktx.h>
#include <meshoptimizer/src/meshoptimizer.h>

#include "offsetAllocator.hpp"
#include "render/model/model_format.h"
#include "render/model/model_serialization.h"
#include "render/shaders/constants_interop.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace Render
{
AssetGenerator::AssetGenerator(VulkanContext* context, enki::TaskScheduler* taskscheduler)
    : context(context), taskscheduler(taskscheduler), generateTask(this)
{
    VkFenceCreateInfo fenceInfo = VkHelpers::FenceCreateInfo();
    fenceInfo.flags = 0;
    VK_CHECK(vkCreateFence(context->device, &fenceInfo, nullptr, &immediateParameters.immFence));

    const VkCommandPoolCreateInfo poolInfo = VkHelpers::CommandPoolCreateInfo(context->graphicsQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &immediateParameters.immCommandPool));

    const VkCommandBufferAllocateInfo allocInfo = VkHelpers::CommandBufferAllocateInfo(1, immediateParameters.immCommandPool);
    VK_CHECK(vkAllocateCommandBuffers(context->device, &allocInfo, &immediateParameters.immCommandBuffer));

    immediateParameters.imageStagingBuffer = AllocatedBuffer::CreateAllocatedStagingBuffer(context, MODEL_GENERATION_STAGING_BUFFER_SIZE);
    immediateParameters.imageReceivingBuffer = AllocatedBuffer::CreateAllocatedReceivingBuffer(context, MODEL_GENERATION_STAGING_BUFFER_SIZE);
}

AssetGenerator::~AssetGenerator()
{
    taskscheduler->WaitforTask(&generateTask);
    vkDestroyCommandPool(context->device, immediateParameters.immCommandPool, nullptr);
    vkDestroyFence(context->device, immediateParameters.immFence, nullptr);
}

void AssetGenerator::WaitForAsyncModelGeneration() const
{
    taskscheduler->WaitforTask(&generateTask);
}

GenerateResponse AssetGenerator::GenerateWillModelAsync(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath)
{
    bool expected = false;
    if (!bIsGenerating.compare_exchange_strong(expected, true)) {
        return GenerateResponse::UNABLE_TO_START;
    }

    generateTask.gltfPath = gltfPath;
    generateTask.outputPath = outputPath;

    taskscheduler->AddTaskSetToPipe(&generateTask);

    return GenerateResponse::STARTED;
}

GenerateResponse AssetGenerator::GenerateWillModel(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath)
{
    bool expected = false;
    if (!bIsGenerating.compare_exchange_strong(expected, true)) {
        return GenerateResponse::UNABLE_TO_START;
    }

    GenerateWillModel_Internal(gltfPath, outputPath);

    bIsGenerating.store(false, std::memory_order::release);
    return GenerateResponse::FINISHED;
}

GenerateResponse AssetGenerator::GenerateKtxTexture(const std::filesystem::path& imageSource, const std::filesystem::path& outputPath, bool mipmapped)
{
    int width, height, nrChannels;
    stbi_uc* stbiData = stbi_load(imageSource.string().c_str(), &width, &height, &nrChannels, 4);

    if (!stbiData) {
        SPDLOG_ERROR("[AssetGenerator::GenerateKtxTexture] Failed to load image: {}", imageSource.string());
        return GenerateResponse::UNABLE_TO_START;
    }

    VkExtent3D imagesize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    const size_t size = width * height * 4;

    OffsetAllocator::Allocation allocation = immediateParameters.imageStagingAllocator.allocate(size);
    if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        SPDLOG_ERROR("[AssetGenerator::GenerateKtxTexture] Texture too large for staging buffer");
        stbi_image_free(stbiData);
        return GenerateResponse::UNABLE_TO_START;
    }

    VkCommandBufferBeginInfo cmdBeginInfo = VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(immediateParameters.immCommandBuffer, &cmdBeginInfo));

    AllocatedImage image = RecordCreateImageFromData(
        immediateParameters.immCommandBuffer,
        allocation.offset,
        stbiData,
        size,
        imagesize,
        VK_FORMAT_R8G8B8A8_UNORM,
        static_cast<VkImageUsageFlagBits>(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
        mipmapped
    );

    stbi_image_free(stbiData);

    uint32_t mipLevels = mipmapped ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;

    if (mipmapped && mipLevels > 1) {
        // Generate mipmaps via blitting
        VkImageMemoryBarrier2 firstBarrier = VkHelpers::ImageMemoryBarrier(
            image.handle,
            VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, image.layout,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &firstBarrier};
        vkCmdPipelineBarrier2(immediateParameters.immCommandBuffer, &depInfo);

        for (uint32_t mip = 1; mip < mipLevels; mip++) {
            VkImageMemoryBarrier2 barriers[2];
            barriers[0] = VkHelpers::ImageMemoryBarrier(
                image.handle,
                VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 1),
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            barriers[1] = VkHelpers::ImageMemoryBarrier(
                image.handle,
                VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1),
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );

            depInfo.imageMemoryBarrierCount = 2;
            depInfo.pImageMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(immediateParameters.immCommandBuffer, &depInfo);

            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {(width >> (mip - 1)), (height >> (mip - 1)), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {(width >> mip), (height >> mip), 1};

            vkCmdBlitImage(immediateParameters.immCommandBuffer, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }

        VkImageMemoryBarrier2 finalBarrier = VkHelpers::ImageMemoryBarrier(
            image.handle,
            VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1),
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &finalBarrier;
        vkCmdPipelineBarrier2(immediateParameters.immCommandBuffer, &depInfo);
        image.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    else {
        // Transition to transfer src for readback
        VkImageMemoryBarrier2 barrier = VkHelpers::ImageMemoryBarrier(
            image.handle,
            VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, image.layout,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
        vkCmdPipelineBarrier2(immediateParameters.immCommandBuffer, &depInfo);
        image.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    // Copy image back to CPU
    std::vector<VkBufferImageCopy> copyRegions;
    copyRegions.reserve(mipLevels);
    size_t bufferOffset = 0;

    for (uint32_t mip = 0; mip < mipLevels; mip++) {
        uint32_t mipWidth = std::max(1u, static_cast<uint32_t>(width) >> mip);
        uint32_t mipHeight = std::max(1u, static_cast<uint32_t>(height) >> mip);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = bufferOffset;
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
        copyRegion.imageExtent = {mipWidth, mipHeight, 1};

        copyRegions.push_back(copyRegion);
        bufferOffset += mipWidth * mipHeight * 4;
    }

    vkCmdCopyImageToBuffer(immediateParameters.immCommandBuffer, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           immediateParameters.imageReceivingBuffer.handle, copyRegions.size(), copyRegions.data());

    VK_CHECK(vkEndCommandBuffer(immediateParameters.immCommandBuffer));

    VkCommandBufferSubmitInfo cmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(immediateParameters.immCommandBuffer);
    VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdSubmitInfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, immediateParameters.immFence));
    VK_CHECK(vkWaitForFences(context->device, 1, &immediateParameters.immFence, true, 1000000000));
    VK_CHECK(vkResetFences(context->device, 1, &immediateParameters.immFence));

    // Create KTX2 texture
    ktxTexture2* texture;
    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    createInfo.baseWidth = width;
    createInfo.baseHeight = height;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = mipLevels;
    createInfo.numLayers = 1;
    createInfo.numFaces = 1;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[AssetGenerator::GenerateKtxTexture] Failed to create KTX texture");
        return GenerateResponse::UNABLE_TO_START;
    }

    // Copy mip data from readback buffer to KTX
    bufferOffset = 0;
    for (uint32_t mip = 0; mip < mipLevels; mip++) {
        uint32_t mipWidth = std::max(1u, static_cast<uint32_t>(width) >> mip);
        uint32_t mipHeight = std::max(1u, static_cast<uint32_t>(height) >> mip);
        size_t mipSize = mipWidth * mipHeight * 4;

        void* readbackData = static_cast<char*>(immediateParameters.imageReceivingBuffer.allocationInfo.pMappedData) + bufferOffset;
        ktxTexture_SetImageFromMemory(ktxTexture(texture), mip, 0, 0, static_cast<const ktx_uint8_t*>(readbackData), mipSize);

        bufferOffset += mipSize;
    }

    /*// Compress to UASTC
    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.uastc = KTX_TRUE;
    params.qualityLevel = 16;
    params.verbose = KTX_FALSE;

    result = ktxTexture2_CompressBasisEx(texture, &params);
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[AssetGenerator::GenerateKtxTexture] Failed to compress texture");
        ktxTexture_Destroy(ktxTexture(texture));
        return GenerateResponse::UNABLE_TO_START;
    }*/

    result = ktxTexture_WriteToNamedFile(ktxTexture(texture), outputPath.string().c_str());
    ktxTexture_Destroy(ktxTexture(texture));

    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[AssetGenerator::GenerateKtxTexture] Failed to write KTX file");
        return GenerateResponse::UNABLE_TO_START;
    }

    SPDLOG_INFO("[AssetGenerator::GenerateKtxTexture] Wrote {}", outputPath.string());
    return GenerateResponse::FINISHED;
}

RawGltfModel AssetGenerator::LoadGltf(const std::filesystem::path& source)
{
    constexpr int32 loadGltfProgressStart = 1;
    constexpr int32 loadGltfProgressTotal = 70;
    int32 _progress = loadGltfProgressStart;
    int32 stepDiff = (loadGltfProgressTotal - loadGltfProgressStart) / 9;
    fastgltf::Parser parser{fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform};
    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember
                                 | fastgltf::Options::AllowDouble
                                 | fastgltf::Options::LoadExternalBuffers
                                 | fastgltf::Options::LoadExternalImages;

    auto gltfFile = fastgltf::MappedGltfFile::FromPath(source);

    // Parse Model
    RawGltfModel rawModel{};
    fastgltf::Asset gltf; {
        if (!static_cast<bool>(gltfFile)) {
            SPDLOG_ERROR("Failed to open glTF file ({}): {}\n", source.filename().string(), getErrorMessage(gltfFile.error()));
            return rawModel;
        }
        auto load = parser.loadGltf(gltfFile.get(), source.parent_path(), gltfOptions);
        if (!load) {
            SPDLOG_ERROR("Failed to load glTF: {}\n", to_underlying(load.error()));
            return rawModel;
        }
        gltf = std::move(load.get());
    }
    _progress += stepDiff;
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

    // Samplers
    {
        rawModel.name = source.filename().string();
        rawModel.samplerInfos.reserve(gltf.samplers.size());
        for (const fastgltf::Sampler& gltfSampler : gltf.samplers) {
            VkSamplerCreateInfo samplerInfo = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr};
            samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
            samplerInfo.minLod = 0;

            samplerInfo.magFilter = ExtractFilter(gltfSampler.magFilter.value_or(fastgltf::Filter::Nearest));
            samplerInfo.minFilter = ExtractFilter(gltfSampler.minFilter.value_or(fastgltf::Filter::Nearest));


            samplerInfo.mipmapMode = ExtractMipmapMode(gltfSampler.minFilter.value_or(fastgltf::Filter::Linear));

            rawModel.samplerInfos.push_back(samplerInfo);
        }
    }
    _progress += stepDiff;
    modelGenerationProgress.value.store(_progress, std::memory_order::release);


    // Images
    {
        rawModel.images.reserve(gltf.images.size());
        immediateParameters.imageStagingAllocator.reset();

        unsigned char* stbiData{nullptr};
        int32_t width{};
        int32_t height{};
        int32_t nrChannels{};

        bool bIsRecording = false;

        std::filesystem::path parentPath = source.parent_path();
        for (const fastgltf::Image& gltfImage : gltf.images) {
            AllocatedImage newImage{};
            std::visit(
                fastgltf::visitor{
                    [&](auto& arg) {},
                    [&](const fastgltf::sources::URI& fileName) {
                        if (fileName.fileByteOffset != 0) {
                            SPDLOG_ERROR("[ModelGenerator::LoadGltf] File byte offset is not currently supported.");
                            return;
                        };

                        if (!fileName.uri.isLocalPath()) {
                            SPDLOG_ERROR("[ModelGenerator::LoadGltf] Loading non-local files is not currently supported.");
                            return;
                        };
                        const std::wstring widePath(fileName.uri.path().begin(), fileName.uri.path().end());
                        const std::filesystem::path fullPath = parentPath / widePath;
                        stbiData = stbi_load(fullPath.string().c_str(), &width, &height, &nrChannels, 4);
                    },
                    [&](const fastgltf::sources::Array& vector) {
                        // Minimum size for a meaningful check
                        if (vector.bytes.size() > 30) {
                            std::string_view strData(reinterpret_cast<const char*>(vector.bytes.data()), std::min(size_t(100), vector.bytes.size()));
                            if (strData.find("https://git-lfs.github.com/spec") != std::string_view::npos) {
                                SPDLOG_ERROR("[ModelGenerator::LoadGltf] Git LFS pointer detected instead of actual texture data for image. `git lfs pull` to retrieve files.");
                                return;
                            }
                        }

                        stbiData = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(vector.bytes.data()), static_cast<int>(vector.bytes.size()), &width, &height, &nrChannels, 4);
                    },
                    [&](const fastgltf::sources::BufferView& view) {
                        const fastgltf::BufferView& bufferView = gltf.bufferViews[view.bufferViewIndex];
                        const fastgltf::Buffer& buffer = gltf.buffers[bufferView.bufferIndex];
                        // We only care about VectorWithMime here, because we
                        // specify LoadExternalBuffers, meaning all buffers
                        // are already loaded into a vector.
                        std::visit(fastgltf::visitor{
                                       [](auto&) {}, [&](const fastgltf::sources::Array& vector) {
                                           stbiData = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(vector.bytes.data() + bufferView.byteOffset), static_cast<int>(bufferView.byteLength),
                                                                            &width,
                                                                            &height,
                                                                            &nrChannels, 4);
                                       }
                                   }, buffer.data);
                    }
                }, gltfImage.data);

            if (!stbiData) { break; }


            VkExtent3D imagesize;
            imagesize.width = width;
            imagesize.height = height;
            imagesize.depth = 1;
            const size_t size = width * height * 4;
            OffsetAllocator::Allocation allocation = immediateParameters.imageStagingAllocator.allocate(size);
            if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                if (bIsRecording) {
                    // Flush staging buffer
                    VK_CHECK(vkEndCommandBuffer(immediateParameters.immCommandBuffer));
                    VkCommandBufferSubmitInfo cmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(immediateParameters.immCommandBuffer);
                    const VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdSubmitInfo, nullptr, nullptr);
                    VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, immediateParameters.immFence));
                    immediateParameters.imageStagingAllocator.reset();
                    VK_CHECK(vkWaitForFences(context->device, 1, &immediateParameters.immFence, true, 1000000000));
                    VK_CHECK(vkResetFences(context->device, 1, &immediateParameters.immFence));
                    VK_CHECK(vkResetCommandBuffer(immediateParameters.immCommandBuffer, 0));
                    bIsRecording = false;


                    // Try again
                    allocation = immediateParameters.imageStagingAllocator.allocate(size);
                    if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                        SPDLOG_ERROR("[ModelGenerator::LoadGltf] Texture too large to fit in staging buffer. Increase staging buffer size or do not load this texture");
                        break;
                    }
                }
                else {
                    SPDLOG_ERROR("[ModelGenerator::LoadGltf] Texture too large to fit in staging buffer. Increase staging buffer size or do not load this texture");
                    break;
                }
            }

            if (!bIsRecording) {
                const VkCommandBufferBeginInfo cmdBeginInfo = VkHelpers::CommandBufferBeginInfo();
                VK_CHECK(vkBeginCommandBuffer(immediateParameters.immCommandBuffer, &cmdBeginInfo));
                bIsRecording = true;
            }

            newImage = RecordCreateImageFromData(immediateParameters.immCommandBuffer, allocation.offset, stbiData, size, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);
            stbi_image_free(stbiData);
            stbiData = nullptr;


            rawModel.images.push_back(std::move(newImage));
        }

        if (rawModel.images.size() != gltf.images.size()) {
            rawModel.images.clear();
            return rawModel;
        }

        if (bIsRecording) {
            // Final flush staging buffer
            VK_CHECK(vkEndCommandBuffer(immediateParameters.immCommandBuffer));
            VkCommandBufferSubmitInfo cmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(immediateParameters.immCommandBuffer);
            const VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdSubmitInfo, nullptr, nullptr);
            VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, immediateParameters.immFence));
            immediateParameters.imageStagingAllocator.reset();
            VK_CHECK(vkWaitForFences(context->device, 1, &immediateParameters.immFence, true, 1000000000));
            VK_CHECK(vkResetFences(context->device, 1, &immediateParameters.immFence));
            VK_CHECK(vkResetCommandBuffer(immediateParameters.immCommandBuffer, 0));
            bIsRecording = false;
        }
    }
    _progress += stepDiff;
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

    // Materials
    rawModel.materials.reserve(gltf.materials.size());
    for (const fastgltf::Material& gltfMaterial : gltf.materials) {
        MaterialProperties material = ExtractMaterial(gltf, gltfMaterial);
        rawModel.materials.push_back(material);
    }
    _progress += stepDiff;
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

    // Meshes
    // WillModel stores as SkinnedVertex, when loading, the vertices will be loaded to different buffers depending on whether the model is a skeletal mesh.
    std::vector<SkinnedVertex> primitiveVertices{};
    std::vector<uint32_t> primitiveIndices{};
    bool hasSkinned = false;
    bool hasStatic = false;
    rawModel.allMeshes.reserve(gltf.meshes.size());
    for (fastgltf::Mesh& mesh : gltf.meshes) {
        MeshInformation meshData{};
        meshData.name = mesh.name;
        meshData.primitiveProperties.reserve(mesh.primitives.size());
        rawModel.primitives.reserve(rawModel.primitives.size() + mesh.primitives.size());

        for (fastgltf::Primitive& p : mesh.primitives) {
            MeshletPrimitive primitiveData{};
            int32_t materialIndex{-1};

            if (p.materialIndex.has_value()) {
                materialIndex = p.materialIndex.value();
                primitiveData.bHasTransparent = (static_cast<MaterialType>(rawModel.materials[materialIndex].alphaProperties.y) == MaterialType::BLEND);
            }


            // INDICES
            const fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
            primitiveIndices.clear();
            primitiveIndices.reserve(indexAccessor.count);

            fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, [&](const std::uint32_t idx) {
                primitiveIndices.push_back(idx);
            });

            // POSITION (REQUIRED)
            const fastgltf::Attribute* positionIt = p.findAttribute("POSITION");
            const fastgltf::Accessor& posAccessor = gltf.accessors[positionIt->accessorIndex];
            primitiveVertices.clear();
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

            // JOINTS_0
            const fastgltf::Attribute* joints0 = p.findAttribute("JOINTS_0");
            if (joints0 != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(gltf, gltf.accessors[joints0->accessorIndex], [&](fastgltf::math::uvec4 j, const size_t index) {
                    primitiveVertices[index].joints = {j.x(), j.y(), j.z(), j.w()};
                });
            }

            // WEIGHTS_0
            const fastgltf::Attribute* weights0 = p.findAttribute("WEIGHTS_0");
            if (weights0 != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, gltf.accessors[weights0->accessorIndex], [&](fastgltf::math::fvec4 w, const size_t index) {
                    primitiveVertices[index].weights = {w.x(), w.y(), w.z(), w.w()};
                });
            }

            if (joints0 != p.attributes.end() && weights0 != p.attributes.end()) {
                hasSkinned = true;
            }
            else {
                hasStatic = true;
            }

            if (hasSkinned && hasStatic) {
                SPDLOG_ERROR("Model contains mixed skinned and static meshes. Split into separate files.");
                return rawModel;
            }

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
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, gltf.accessors[colors->accessorIndex], [&](const fastgltf::math::fvec4& color, const size_t index) {
                    primitiveVertices[index].color = {
                        color.x(), color.y(), color.z(), color.w()
                    };
                });
            }

            size_t max_meshlets = meshopt_buildMeshletsBound(primitiveIndices.size(), MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES);
            std::vector<meshopt_Meshlet> meshlets(max_meshlets);
            std::vector<unsigned int> meshletVertices(primitiveIndices.size());
            std::vector<unsigned char> meshletTriangles(primitiveIndices.size()); {
                ZoneScopedN("BuildMeshlets");
                // build clusters (meshlets) out of the mesh
                std::vector<uint32_t> primitiveVertexPositions;
                meshlets.resize(meshopt_buildMeshlets(&meshlets[0], &meshletVertices[0], &meshletTriangles[0],
                                                      primitiveIndices.data(), primitiveIndices.size(),
                                                      reinterpret_cast<const float*>(primitiveVertices.data()), primitiveVertices.size(), sizeof(SkinnedVertex),
                                                      MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES, 0.f));
            }

            // Optimize each meshlet's micro index buffer/vertex layout individually
            {
                ZoneScopedN("OptimizeMeshlets");
                for (auto& meshlet : meshlets) {
                    meshopt_optimizeMeshlet(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
                }
            }
            // Trim the meshlet data to minimize waste for meshletVertices/meshletTriangles
            const meshopt_Meshlet& last = meshlets.back();
            meshletVertices.resize(last.vertex_offset + last.vertex_count);
            meshletTriangles.resize(last.triangle_offset + last.triangle_count * 3);

            primitiveData.meshletOffset = rawModel.meshlets.size();
            primitiveData.meshletCount = meshlets.size();
            primitiveData.boundingSphere = GenerateBoundingSphere(primitiveVertices);

            meshData.primitiveProperties.emplace_back(rawModel.primitives.size(), materialIndex);
            rawModel.primitives.push_back(primitiveData);

            uint32_t vertexOffset = rawModel.vertices.size();
            uint32_t meshletVertexOffset = rawModel.meshletVertices.size();
            uint32_t meshletTrianglesOffset = rawModel.meshletTriangles.size();

            rawModel.vertices.insert(rawModel.vertices.end(), primitiveVertices.begin(), primitiveVertices.end());
            rawModel.meshletVertices.insert(rawModel.meshletVertices.end(), meshletVertices.begin(), meshletVertices.end());

            rawModel.meshletTriangles.insert(rawModel.meshletTriangles.end(), meshletTriangles.begin(), meshletTriangles.end());

            //
            {
                ZoneScopedN("ComputeMeshletBounds");
                for (meshopt_Meshlet& meshlet : meshlets) {
                    meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                        &meshletVertices[meshlet.vertex_offset],
                        &meshletTriangles[meshlet.triangle_offset],
                        meshlet.triangle_count,
                        reinterpret_cast<const float*>(primitiveVertices.data()),
                        primitiveVertices.size(),
                        sizeof(SkinnedVertex)
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

                        .meshletVerticesOffset = meshletVertexOffset + meshlet.vertex_offset,
                        .meshletTriangleOffset = meshletTrianglesOffset + meshlet.triangle_offset,
                        .meshletVerticesCount = meshlet.vertex_count,
                        .meshletTriangleCount = meshlet.triangle_count,
                    });
                }
            }
        }

        rawModel.allMeshes.push_back(meshData);
    }
    _progress += stepDiff;
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

    // Nodes
    rawModel.nodes.reserve(gltf.nodes.size());
    for (const fastgltf::Node& node : gltf.nodes) {
        Node node_{};
        node_.name = node.name;

        if (node.meshIndex.has_value()) {
            node_.meshIndex = static_cast<int>(*node.meshIndex);
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

                    node_.localTranslation = glm::vec3(glmMatrix[3]);
                    node_.localRotation = glm::quat_cast(glmMatrix);
                    node_.localScale = glm::vec3(
                        glm::length(glm::vec3(glmMatrix[0])),
                        glm::length(glm::vec3(glmMatrix[1])),
                        glm::length(glm::vec3(glmMatrix[2]))
                    );
                },
                [&](fastgltf::TRS transform) {
                    node_.localTranslation = {transform.translation[0], transform.translation[1], transform.translation[2]};
                    node_.localRotation = {transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]};
                    node_.localScale = {transform.scale[0], transform.scale[1], transform.scale[2]};
                }
            }
            , node.transform
        );
        rawModel.nodes.push_back(node_);
    }
    for (int i = 0; i < gltf.nodes.size(); i++) {
        for (std::size_t& child : gltf.nodes[i].children) {
            rawModel.nodes[child].parent = i;
        }
    }
    _progress += stepDiff;
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

    // Skins
    // only import first skin
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
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

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
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

    // Animations
    rawModel.animations.reserve(gltf.animations.size());
    for (fastgltf::Animation& gltfAnim : gltf.animations) {
        Animation anim{};
        anim.name = gltfAnim.name;

        for (fastgltf::AnimationSampler& animSampler : gltfAnim.samplers) {
            AnimationSampler sampler;

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
                    sampler.interpolation = AnimationSampler::Interpolation::Linear;
                    break;
                case fastgltf::AnimationInterpolation::Step:
                    sampler.interpolation = AnimationSampler::Interpolation::Step;
                    break;
                case fastgltf::AnimationInterpolation::CubicSpline:
                    sampler.interpolation = AnimationSampler::Interpolation::CubicSpline;
                    break;
            }

            anim.samplers.push_back(std::move(sampler));
        }

        anim.channels.reserve(gltfAnim.channels.size());
        for (auto& gltfChannel : gltfAnim.channels) {
            AnimationChannel channel{};
            channel.samplerIndex = gltfChannel.samplerIndex;
            channel.targetNodeIndex = nodeRemap[gltfChannel.nodeIndex.value()];

            switch (gltfChannel.path) {
                case fastgltf::AnimationPath::Translation:
                    channel.targetPath = AnimationChannel::TargetPath::Translation;
                    break;
                case fastgltf::AnimationPath::Rotation:
                    channel.targetPath = AnimationChannel::TargetPath::Rotation;
                    break;
                case fastgltf::AnimationPath::Scale:
                    channel.targetPath = AnimationChannel::TargetPath::Scale;
                    break;
                case fastgltf::AnimationPath::Weights:
                    channel.targetPath = AnimationChannel::TargetPath::Weights;
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
    modelGenerationProgress.value.store(_progress, std::memory_order::release);

    rawModel.bIsSkeletalModel = hasSkinned;
    rawModel.bSuccessfullyLoaded = true;
    return rawModel;
}

bool AssetGenerator::WriteWillModel(RawGltfModel& rawModel, const std::filesystem::path& outputPath)
{
    //
    {
        ZoneScopedN("CleanupTempDirectory");
        if (std::filesystem::exists("temp")) {
            std::filesystem::remove_all("temp");
        }
        std::filesystem::create_directories("temp");
    }

    //
    {
        ZoneScopedN("WriteModelBinary");
        std::ofstream binFile("temp/model.bin", std::ios::binary);
        WriteModelBinary(binFile, rawModel);
        binFile.close();
    }

    float _progress = 70.0f;
    constexpr float textureProgressTotal = 30.0f;
    const float progressPerTexture = rawModel.images.empty() ? 0.0f : textureProgressTotal / static_cast<float>(rawModel.images.size());

    for (size_t i = 0; i < rawModel.images.size(); i++) {
        ZoneScopedN("ProcessTexture");
        auto& image = rawModel.images[i];
        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(image.extent.width, image.extent.height)))) + 1;

        // Mip Generation
        {
            ZoneScopedN("GenerateMipmaps");
            VK_CHECK(vkResetCommandBuffer(immediateParameters.immCommandBuffer, 0));

            const auto cmd = immediateParameters.immCommandBuffer;
            const VkCommandBufferBeginInfo cmdBeginInfo = VkHelpers::CommandBufferBeginInfo();
            VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));


            VkImageMemoryBarrier2 firstBarrier = VkHelpers::ImageMemoryBarrier(
                image.handle,
                VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, image.layout,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            VkDependencyInfo firstDepInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            firstDepInfo.imageMemoryBarrierCount = 1;
            firstDepInfo.pImageMemoryBarriers = &firstBarrier;
            vkCmdPipelineBarrier2(cmd, &firstDepInfo);

            for (uint32_t mip = 1; mip < mipLevels; mip++) {
                VkImageMemoryBarrier2 barriers[2];
                barriers[0] = VkHelpers::ImageMemoryBarrier(
                    image.handle,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 1),
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                );
                barriers[1] = VkHelpers::ImageMemoryBarrier(
                    image.handle,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1),
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                );

                VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = 2;
                depInfo.pImageMemoryBarriers = barriers;
                vkCmdPipelineBarrier2(cmd, &depInfo);

                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {
                    static_cast<int32_t>(image.extent.width >> (mip - 1)),
                    static_cast<int32_t>(image.extent.height >> (mip - 1)),
                    1
                };
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {
                    static_cast<int32_t>(image.extent.width >> mip),
                    static_cast<int32_t>(image.extent.height >> mip),
                    1
                };

                vkCmdBlitImage(cmd, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            }

            VkImageMemoryBarrier2 finalBarrier = VkHelpers::ImageMemoryBarrier(
                image.handle,
                VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 1),
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &finalBarrier;
            vkCmdPipelineBarrier2(cmd, &depInfo);
            image.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            VK_CHECK(vkEndCommandBuffer(cmd));

            VkCommandBufferSubmitInfo cmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(cmd);
            const VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdSubmitInfo, nullptr, nullptr);
            VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, immediateParameters.immFence));
            VK_CHECK(vkWaitForFences(context->device, 1, &immediateParameters.immFence, true, 1000000000));
            VK_CHECK(vkResetFences(context->device, 1, &immediateParameters.immFence));
            SPDLOG_TRACE("[ModelGenerator::WriteWillModel] Created mipmap chain for image {}", i);
        }

        ktxTexture2* texture;
        ktxTextureCreateInfo createInfo{};
        createInfo.vkFormat = image.format;
        createInfo.baseWidth = image.extent.width;
        createInfo.baseHeight = image.extent.height;
        createInfo.baseDepth = 1;
        createInfo.numDimensions = 2;
        createInfo.numLevels = mipLevels;
        createInfo.numLayers = 1;
        createInfo.numFaces = 1;
        createInfo.isArray = KTX_FALSE;
        createInfo.generateMipmaps = KTX_FALSE;

        ktx_error_code_e result;
        //
        {
            ZoneScopedN("KTXCreate");
            result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
        }

        if (result) {
            SPDLOG_ERROR("[ModelGenerator::WriteWillModel] Failed to create ktx texture for texture ", i);
            return false;
        }

        //
        {
            ZoneScopedN("CopyImageToCPU");
            VK_CHECK(vkResetCommandBuffer(immediateParameters.immCommandBuffer, 0));
            VkCommandBufferBeginInfo cmdBeginInfo = VkHelpers::CommandBufferBeginInfo();
            VK_CHECK(vkBeginCommandBuffer(immediateParameters.immCommandBuffer, &cmdBeginInfo));

            std::vector<VkBufferImageCopy> copyRegions;
            copyRegions.reserve(mipLevels);
            size_t bufferOffset = 0;
            uint32_t bytesPerPixel = VkHelpers::GetBytesPerPixel(image.format);

            for (uint32_t mip = 0; mip < mipLevels; mip++) {
                uint32_t mipWidth = std::max(1u, image.extent.width >> mip);
                uint32_t mipHeight = std::max(1u, image.extent.height >> mip);

                VkBufferImageCopy copyRegion{};
                copyRegion.bufferOffset = bufferOffset;
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = mip;
                copyRegion.imageSubresource.baseArrayLayer = 0;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageExtent = {mipWidth, mipHeight, 1};

                copyRegions.push_back(copyRegion);
                bufferOffset += mipWidth * mipHeight * bytesPerPixel;
            }

            vkCmdCopyImageToBuffer(immediateParameters.immCommandBuffer, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   immediateParameters.imageReceivingBuffer.handle, copyRegions.size(), copyRegions.data());

            VK_CHECK(vkEndCommandBuffer(immediateParameters.immCommandBuffer));

            VkCommandBufferSubmitInfo cmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(immediateParameters.immCommandBuffer);
            VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdSubmitInfo, nullptr, nullptr);
            VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, immediateParameters.immFence));
            VK_CHECK(vkWaitForFences(context->device, 1, &immediateParameters.immFence, true, 1000000000));
            VK_CHECK(vkResetFences(context->device, 1, &immediateParameters.immFence));
        }

        //
        {
            ZoneScopedN("CopyToKTX");
            size_t bufferOffset = 0;
            for (uint32_t mip = 0; mip < mipLevels; mip++) {
                uint32_t mipWidth = std::max(1u, image.extent.width >> mip);
                uint32_t mipHeight = std::max(1u, image.extent.height >> mip);
                size_t mipSize = mipWidth * mipHeight * 4;

                void* readbackData = static_cast<char*>(immediateParameters.imageReceivingBuffer.allocationInfo.pMappedData) + bufferOffset;
                ktxTexture_SetImageFromMemory(ktxTexture(texture), mip, 0, 0, static_cast<const ktx_uint8_t*>(readbackData), mipSize);

                bufferOffset += mipSize;
            }
        }


        std::string ktxPath = "temp/texture_" + std::to_string(i) + ".ktx2";
        //
        {
            ZoneScopedN("CompressUASTC");

            ktxBasisParams params{};
            params.structSize = sizeof(params);
            params.uastc = KTX_TRUE;
            // params.uastcRDO = KTX_TRUE;
            params.qualityLevel = 16;
            params.verbose = KTX_FALSE;

            result = ktxTexture2_CompressBasisEx(texture, &params);
        }
        //
        {
            ZoneScopedN("WriteKTXFile");
            ktxTexture_WriteToNamedFile(ktxTexture(texture), ktxPath.c_str());
        }

        SPDLOG_TRACE("Wrote {}", ktxPath);
        ktxTexture_Destroy(ktxTexture(texture));

        _progress += progressPerTexture;
        modelGenerationProgress.value.store(_progress, std::memory_order::release);
    }

    //
    {
        ZoneScopedN("CreateArchive");

        ModelWriter writer{outputPath};
        writer.AddFileFromDisk("model.bin", "temp/model.bin", true);

        uint32_t i = 0;
        while (true) {
            std::string sourcePath = fmt::format("temp/texture_{}.ktx2", i);
            if (!std::filesystem::exists(sourcePath)) {
                break;
            }

            std::string archiveName = fmt::format("textures/texture_{}.ktx2", i);
            bool writeRes = writer.AddFileFromDisk(archiveName, sourcePath, true);
            if (!writeRes) {
                return false;
            }
            i++;
        }

        bool success = writer.Finalize();

        // Ensure progress reaches 100%
        modelGenerationProgress.value.store(100, std::memory_order::release);

        return success;
    }
}

void AssetGenerator::GenerateWillModel_Internal(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath)
{
    ZoneScopedN("GenerateWillModel_Internal");

    modelGenerationProgress.loadingState.store(WillModelGenerationProgress::LoadingProgress::LOADING_GLTF, std::memory_order::release);
    modelGenerationProgress.value.store(1, std::memory_order::release);
    RawGltfModel rawModel;
    //
    {
        ZoneScopedN("LoadGltf");
        rawModel = LoadGltf(gltfPath);
    }

    if (!rawModel.bSuccessfullyLoaded) {
        modelGenerationProgress.loadingState.store(WillModelGenerationProgress::LoadingProgress::FAILED, std::memory_order::release);
        modelGenerationProgress.value.store(0, std::memory_order::release);
        return;
    }

    modelGenerationProgress.loadingState.store(WillModelGenerationProgress::LoadingProgress::WRITING_WILL_MODEL, std::memory_order::release);
    modelGenerationProgress.value.store(70, std::memory_order::release);
    bool success;
    //
    {
        ZoneScopedN("WriteWillModel");
        success = WriteWillModel(rawModel, outputPath);
    }

    modelGenerationProgress.loadingState.store(success ? WillModelGenerationProgress::LoadingProgress::SUCCESS : WillModelGenerationProgress::LoadingProgress::FAILED, std::memory_order::release);
    modelGenerationProgress.value.store(100, std::memory_order::release);
}

void WriteModelBinary(std::ofstream& file, const RawGltfModel& model)
{
    ModelBinaryHeader header{};
    header.vertexCount = static_cast<uint32_t>(model.vertices.size());
    header.meshletVertexCount = static_cast<uint32_t>(model.meshletVertices.size());
    header.meshletTriangleCount = static_cast<uint32_t>(model.meshletTriangles.size());
    header.meshletCount = static_cast<uint32_t>(model.meshlets.size());
    header.primitiveCount = static_cast<uint32_t>(model.primitives.size());
    header.materialCount = static_cast<uint32_t>(model.materials.size());
    header.meshCount = static_cast<uint32_t>(model.allMeshes.size());
    header.nodeCount = static_cast<uint32_t>(model.nodes.size());
    header.animationCount = static_cast<uint32_t>(model.animations.size());
    header.inverseBindMatrixCount = static_cast<uint32_t>(model.inverseBindMatrices.size());
    header.samplerCount = static_cast<uint32_t>(model.samplerInfos.size());
    header.textureCount = static_cast<uint32_t>(model.images.size());
    header.bIsSkeletalModel = model.bIsSkeletalModel ? 1u : 0u;

    file.write(reinterpret_cast<const char*>(&header), sizeof(ModelBinaryHeader));

    WriteVector(file, model.vertices);
    WriteVector(file, model.meshletVertices);
    WriteVector(file, model.meshletTriangles);
    WriteVector(file, model.meshlets);
    WriteVector(file, model.primitives);
    WriteVector(file, model.materials);

    for (const auto& mesh : model.allMeshes) {
        WriteMeshInformation(file, mesh);
    }
    for (const auto& node : model.nodes) {
        WriteNode(file, node);
    }

    for (const auto& anim : model.animations) {
        WriteAnimation(file, anim);
    }

    WriteVector(file, model.inverseBindMatrices);
    WriteVector(file, model.samplerInfos);

    std::vector<uint32_t> preferredImageFormats;
    preferredImageFormats.resize(model.images.size(), KTX_TTF_BC7_RGBA);

    for (const auto& material : model.materials) {
        // Color/emissive textures -> BC7 SRGB
        if (material.textureImageIndices.x >= 0) {
            preferredImageFormats[material.textureImageIndices.x] = KTX_TTF_BC7_RGBA;
        }
        if (material.textureImageIndices.w >= 0) {
            preferredImageFormats[material.textureImageIndices.w] = KTX_TTF_BC7_RGBA;
        }

        // Normal map -> BC5
        if (material.textureImageIndices.z >= 0) {
            preferredImageFormats[material.textureImageIndices.z] = KTX_TTF_BC5_RG;
        }

        // Metallic-roughness -> BC7 (linear)
        if (material.textureImageIndices.y >= 0) {
            preferredImageFormats[material.textureImageIndices.y] = KTX_TTF_BC7_RGBA;
        }

        // Occlusion -> BC4
        if (material.textureImageIndices2.x >= 0) {
            preferredImageFormats[material.textureImageIndices2.x] = KTX_TTF_BC4_R;
        }

        // Packed NRM (if used) -> BC7
        if (material.textureImageIndices2.y >= 0) {
            preferredImageFormats[material.textureImageIndices2.y] = KTX_TTF_BC7_RGBA;
        }
    }

    WriteVector(file, preferredImageFormats);
}

VkFilter AssetGenerator::ExtractFilter(fastgltf::Filter filter)
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

VkSamplerMipmapMode AssetGenerator::ExtractMipmapMode(fastgltf::Filter filter)
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

MaterialProperties AssetGenerator::ExtractMaterial(fastgltf::Asset& gltf, const fastgltf::Material& gltfMaterial)
{
    MaterialProperties material = {};
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
            material.alphaProperties.y = static_cast<float>(MaterialType::SOLID);
            break;
        case fastgltf::AlphaMode::Blend:
            material.alphaProperties.y = static_cast<float>(MaterialType::BLEND);
            break;
        case fastgltf::AlphaMode::Mask:
            material.alphaProperties.y = static_cast<float>(MaterialType::CUTOUT);
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

void AssetGenerator::LoadTextureIndicesAndUV(const fastgltf::TextureInfo& texture, const fastgltf::Asset& gltf, int& imageIndex, int& samplerIndex, glm::vec4& uvTransform)
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

glm::vec4 AssetGenerator::GenerateBoundingSphere(const std::vector<Vertex>& vertices)
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

glm::vec4 AssetGenerator::GenerateBoundingSphere(const std::vector<SkinnedVertex>& vertices)
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

void AssetGenerator::TopologicalSortNodes(std::vector<Node>& nodes, std::vector<uint32_t>& oldToNew)
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

AllocatedImage AssetGenerator::RecordCreateImageFromData(VkCommandBuffer cmd, size_t offset, unsigned char* data, size_t size, VkExtent3D imageExtent, VkFormat format, VkImageUsageFlagBits usage,
                                                         bool mipmapped)
{
    char* bufferOffset = static_cast<char*>(immediateParameters.imageStagingBuffer.allocationInfo.pMappedData) + offset;
    memcpy(bufferOffset, data, size);

    VkImageCreateInfo imageCreateInfo = VkHelpers::ImageCreateInfo(format, imageExtent, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT); // transfer src for mipmap only
    if (mipmapped) {
        imageCreateInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(imageExtent.width, imageExtent.height)))) + 1;
    }
    AllocatedImage newImage = AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageMemoryBarrier2 barrier = VkHelpers::ImageMemoryBarrier(
        newImage.handle,
        VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = offset;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = imageExtent;

    vkCmdCopyBufferToImage(cmd, immediateParameters.imageStagingBuffer.handle, newImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    newImage.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    return newImage;
}
} // Render
