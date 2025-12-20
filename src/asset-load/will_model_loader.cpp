//
// Created by William on 2025-12-19.
//

#include "will_model_loader.h"

#include <fstream>

#include <ktxvulkan.h>
#include <glm/glm.hpp>

#include "render/model/model_serialization.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
void LoadModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
{
    if (modelLoader) {
        modelLoader->TaskImplementation();
    }
}

WillModelLoader::WillModelLoader()
{
    uploadStaging = std::make_unique<UploadStaging>();
    loadModelTask = std::make_unique<LoadModelTask>();
}

WillModelLoader::~WillModelLoader() = default;

void WillModelLoader::Reset()
{
    rawData.Reset();
    loadState = WillModelLoadState::Idle;
    taskState = TaskState::NotStarted;
    willModelHandle = Render::WillModelHandle::INVALID;
    model = nullptr;
    pendingSamplerInfos.clear();
    for (ktxTexture2* texture : pendingTextures) {
        ktxTexture2_Destroy(texture);
    }
    pendingTextureHead = 0;
    pendingVerticesHead = 0;
    pendingMeshletVerticesHead = 0;
    pendingMeshletTrianglesHead = 0;
    pendingMeshletsHead = 0;
    pendingPrimitivesHead = 0;
    pendingBufferBarrier = 0;

    pendingTextures.clear();
}

WillModelLoader::TaskState WillModelLoader::TaskExecute(enki::TaskScheduler* scheduler, LoadModelTask* task)
{
    if (taskState == TaskState::NotStarted) {
        task->modelLoader = this;
        taskState = TaskState::InProgress;
        scheduler->AddTaskSetToPipe(task);
    }

    if (task->GetIsComplete()) {
        return taskState;
    }

    return TaskState::InProgress;
}

void WillModelLoader::TaskImplementation()
{
    // todo: tracy profile this step
    if (!std::filesystem::exists(model->source)) {
        SPDLOG_ERROR("Failed to find path to willmodel - {}", model->name);
        taskState = TaskState::Failed;
        return;
    }

    Render::ModelReader reader = Render::ModelReader(model->source);

    if (!reader.GetSuccessfullyLoaded()) {
        SPDLOG_ERROR("Failed to load willmodel - {}", model->name);
        taskState = TaskState::Failed;
        return;
    }

    std::vector<uint8_t> modelBinData = reader.ReadFile("model.bin");

    size_t offset = 0;
    const auto* header = reinterpret_cast<Render::ModelBinaryHeader*>(modelBinData.data());
    offset += sizeof(Render::ModelBinaryHeader);

    auto readArray = [&]<typename T>(std::vector<T>& vec, uint32_t count) {
        vec.resize(count);
        if (count > 0) {
            std::memcpy(vec.data(), modelBinData.data() + offset, count * sizeof(T));
            offset += count * sizeof(T);
        }
    };


    const uint8_t* dataPtr = modelBinData.data() + offset;

    rawData.bIsSkeletalModel = header->bIsSkeletalModel;
    readArray(rawData.vertices, header->vertexCount);
    readArray(rawData.meshletVertices, header->meshletVertexCount);
    readArray(rawData.meshletTriangles, header->meshletTriangleCount);
    readArray(rawData.meshlets, header->meshletCount);
    readArray(rawData.primitives, header->primitiveCount);
    readArray(rawData.materials, header->materialCount);

    dataPtr = modelBinData.data() + offset;
    rawData.allMeshes.resize(header->meshCount);
    for (uint32_t i = 0; i < header->meshCount; i++) {
        ReadMeshInformation(dataPtr, rawData.allMeshes[i]);
    }

    rawData.nodes.resize(header->nodeCount);
    for (uint32_t i = 0; i < header->nodeCount; i++) {
        ReadNode(dataPtr, rawData.nodes[i]);
    }

    offset = dataPtr - modelBinData.data();
    readArray(rawData.nodeRemap, header->nodeRemapCount);

    dataPtr = modelBinData.data() + offset;
    rawData.animations.resize(header->animationCount);
    for (uint32_t i = 0; i < header->animationCount; i++) {
        ReadAnimation(dataPtr, rawData.animations[i]);
    }

    offset = dataPtr - modelBinData.data();
    readArray(rawData.inverseBindMatrices, header->inverseBindMatrixCount);

    readArray(pendingSamplerInfos, header->samplerCount);
    std::vector<uint32_t> preferredImageFormats;
    readArray(preferredImageFormats, header->textureCount);

    for (int i = 0; i < header->textureCount; ++i) {
        std::string textureName = fmt::format("textures/texture_{}.ktx2", i);
        if (!reader.HasFile(textureName)) {
            SPDLOG_ERROR("[WillModelLoader::TaskImplementation] Failed to find texture {}", textureName);
            pendingTextures.push_back(nullptr);
            continue;
        }

        std::vector<uint8_t> ktxData = reader.ReadFile(textureName);

        std::filesystem::create_directories("temp");
        std::string tempKtxPath = fmt::format("temp/loaded_texture_{}.ktx2", i);
        std::ofstream tempFile(tempKtxPath, std::ios::binary);
        tempFile.write(reinterpret_cast<const char*>(ktxData.data()), ktxData.size());
        tempFile.close();

        ktxTexture2* loadedTexture = nullptr;
        ktx_error_code_e result = ktxTexture2_CreateFromNamedFile(tempKtxPath.c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &loadedTexture);

        if (ktxTexture2_NeedsTranscoding(loadedTexture)) {
            const ktx_transcode_fmt_e targetFormat = static_cast<ktx_transcode_fmt_e>(preferredImageFormats[i]);
            result = ktxTexture2_TranscodeBasis(loadedTexture, targetFormat, 0);
            if (result != KTX_SUCCESS) {
                SPDLOG_ERROR("Failed to transcode texture {}", textureName);
                pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }
        }

        // Size check
        ktx_size_t allocSize = loadedTexture->dataSize;
        if (allocSize >= ASSET_LOAD_STAGING_BUFFER_SIZE) {
            SPDLOG_ERROR("Texture too big to fit in the staging buffer for texture {}", textureName);
            pendingTextures.push_back(nullptr);
            ktxTexture2_Destroy(loadedTexture);
            continue;
        }

        // Texture dimension and array check
        if (loadedTexture->numDimensions != 2) {
            SPDLOG_ERROR("Engine does not support non 2D image textures {}", textureName);
            pendingTextures.push_back(nullptr);
            ktxTexture2_Destroy(loadedTexture);
            continue;
        }

        if (loadedTexture->isArray) {
            SPDLOG_ERROR("Engine does not support texture arrays {}", textureName);
            pendingTextures.push_back(nullptr);
            ktxTexture2_Destroy(loadedTexture);
            continue;
        }

        if (loadedTexture->isCubemap) {
            SPDLOG_ERROR("Texture does not support cubemaps {}", textureName);
            pendingTextures.push_back(nullptr);
            ktxTexture2_Destroy(loadedTexture);
            continue;
        }

        pendingTextures.push_back(loadedTexture);
    }

    rawData.name = "Loaded Model";
    taskState = TaskState::Complete;
}

bool WillModelLoader::PreThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager)
{
    OffsetAllocator::Allocator* selectedAllocator;
    size_t sizeVertices;
    if (rawData.bIsSkeletalModel) {
        sizeVertices = rawData.vertices.size() * sizeof(SkinnedVertex);
        selectedAllocator = &resourceManager->skinnedVertexBufferAllocator;
    }
    else {
        sizeVertices = rawData.vertices.size() * sizeof(Vertex);
        selectedAllocator = &resourceManager->vertexBufferAllocator;
    }

    model->modelData.vertexAllocation = selectedAllocator->allocate(sizeVertices);
    if (model->modelData.vertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega vertex buffer to upload {}", model->name);
        return false;
    }

    size_t sizeMeshletVertices = rawData.meshletVertices.size() * sizeof(uint32_t);
    model->modelData.meshletVertexAllocation = resourceManager->meshletVertexBufferAllocator.allocate(sizeMeshletVertices);
    if (model->modelData.meshletVertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(model->modelData.vertexAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega meshlet vertex buffer to upload {}", model->name);
        return false;
    }

    size_t sizeMeshletTriangles = rawData.meshletTriangles.size() * sizeof(uint8_t);
    model->modelData.meshletTriangleAllocation = resourceManager->meshletTriangleBufferAllocator.allocate(sizeMeshletTriangles);
    if (model->modelData.meshletTriangleAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(model->modelData.vertexAllocation);
        resourceManager->meshletVertexBufferAllocator.free(model->modelData.meshletVertexAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega meshlet triangle buffer to upload {}", model->name);
        return false;
    }

    size_t sizeMeshlets = rawData.meshlets.size() * sizeof(Meshlet);
    model->modelData.meshletAllocation = resourceManager->meshletBufferAllocator.allocate(sizeMeshlets);
    if (model->modelData.meshletAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(model->modelData.vertexAllocation);
        resourceManager->meshletVertexBufferAllocator.free(model->modelData.meshletVertexAllocation);
        resourceManager->meshletTriangleBufferAllocator.free(model->modelData.meshletTriangleAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega meshlet buffer to upload {}", model->name);
        return false;
    }

    size_t sizePrimitives = rawData.primitives.size() * sizeof(MeshletPrimitive);
    model->modelData.primitiveAllocation = resourceManager->primitiveBufferAllocator.allocate(sizePrimitives);
    if (model->modelData.primitiveAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(model->modelData.vertexAllocation);
        resourceManager->meshletVertexBufferAllocator.free(model->modelData.meshletVertexAllocation);
        resourceManager->meshletTriangleBufferAllocator.free(model->modelData.meshletTriangleAllocation);
        resourceManager->meshletBufferAllocator.free(model->modelData.meshletAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega primitive buffer to upload {}", model->name);
        return false;
    }

    uint32_t vertexOffset = model->modelData.vertexAllocation.offset / (rawData.bIsSkeletalModel ? sizeof(SkinnedVertex) : sizeof(Vertex));
    uint32_t meshletVerticesOffset = model->modelData.meshletVertexAllocation.offset / sizeof(uint32_t);
    uint32_t meshletTriangleOffset = model->modelData.meshletTriangleAllocation.offset / sizeof(uint8_t);

    for (Meshlet& meshlet : rawData.meshlets) {
        meshlet.vertexOffset += vertexOffset;
        meshlet.meshletVerticesOffset += meshletVerticesOffset;
        meshlet.meshletTriangleOffset += meshletTriangleOffset;
    }

    uint32_t meshletOffset = model->modelData.meshletAllocation.offset / sizeof(Meshlet);
    for (auto& primitive : rawData.primitives) {
        primitive.meshletOffset += meshletOffset;
    }

    uint32_t primitiveOffsetCount = model->modelData.primitiveAllocation.offset / sizeof(MeshletPrimitive);
    for (auto& mesh : rawData.allMeshes) {
        for (auto& primitiveIndex : mesh.primitiveIndices) {
            primitiveIndex += primitiveOffsetCount;
        }
    }

    model->modelData.meshes = std::move(rawData.allMeshes);
    model->modelData.nodes = std::move(rawData.nodes);
    model->modelData.inverseBindMatrices = std::move(rawData.inverseBindMatrices);
    model->modelData.animations = std::move(rawData.animations);
    model->modelData.nodeRemap = std::move(rawData.nodeRemap);


    return true;
}

WillModelLoader::ThreadState WillModelLoader::ThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager)
{
    OffsetAllocator::Allocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    // KTX texture upload
    {
        // Do not block thread waiting for fence
        if (!uploadStaging->IsReady()) {
            return ThreadState::InProgress;
        }

        for (; pendingTextureHead < pendingTextures.size(); pendingTextureHead++) {
            ktxTexture2* currentTexture = pendingTextures[pendingTextureHead];

            if (currentTexture == nullptr) {
                model->modelData.images.emplace_back();
                model->modelData.imageViews.emplace_back();
                continue;
            }

            ktx_size_t dataSize = currentTexture->dataSize;
            OffsetAllocator::Allocation allocation = stagingAllocator.allocate(dataSize);
            if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                assert(stagingAllocator.storageReport().totalFreeSpace != ASSET_LOAD_STAGING_BUFFER_SIZE);
                uploadStaging->SubmitCommandBuffer();
                return ThreadState::InProgress;
            }


            VkExtent3D extent;
            extent.width = currentTexture->baseWidth;
            extent.height = currentTexture->baseHeight;
            extent.depth = currentTexture->baseDepth;

            VkFormat imageFormat = ktxTexture2_GetVkFormat(currentTexture);
            VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(imageFormat, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            imageCreateInfo.mipLevels = currentTexture->numLevels;
            imageCreateInfo.arrayLayers = currentTexture->numLayers;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Render::AllocatedImage allocatedImage = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

            VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(allocatedImage.handle, allocatedImage.format, VK_IMAGE_ASPECT_COLOR_BIT);
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.subresourceRange.layerCount = currentTexture->numLayers;
            viewInfo.subresourceRange.levelCount = currentTexture->numLevels;
            Render::ImageView imageView = Render::ImageView::CreateImageView(context, viewInfo);

            std::vector<VkBufferImageCopy> copyRegions;
            copyRegions.resize(currentTexture->numLevels);

            uploadStaging->StartCommandBuffer();
            char* bufferOffset = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation.offset;
            memcpy(bufferOffset, currentTexture->pData, dataSize);


            size_t textureOffsetInStaging = allocation.offset;

            for (uint32_t mip = 0; mip < currentTexture->numLevels; mip++) {
                size_t mipOffset;
                ktxTexture_GetImageOffset(ktxTexture(currentTexture), mip, 0, 0, &mipOffset);

                VkBufferImageCopy& region = copyRegions[mip];
                region.bufferOffset = textureOffsetInStaging + mipOffset;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = mip;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageSubresource.layerCount = currentTexture->numLayers;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {
                    std::max(1u, currentTexture->baseWidth >> mip),
                    std::max(1u, currentTexture->baseHeight >> mip),
                    std::max(1u, currentTexture->baseDepth >> mip)
                };
            }

            VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
                allocatedImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, currentTexture->numLevels, 0, currentTexture->numLayers),
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

            vkCmdCopyBufferToImage(uploadStaging->GetCommandBuffer(), stagingBuffer.handle, allocatedImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copyRegions.size(), copyRegions.data());

            barrier = Render::VkHelpers::ImageMemoryBarrier(
                allocatedImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, currentTexture->numLevels, 0, currentTexture->numLayers),
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            barrier.srcQueueFamilyIndex = context->transferQueueFamily;
            barrier.dstQueueFamilyIndex = context->graphicsQueueFamily;
            vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

            // Image acquires that needs to be executed by render thread
            model->imageAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));

            model->modelData.images.push_back(std::move(allocatedImage));
            model->modelData.imageViews.push_back(std::move(imageView));
            ktxTexture2_Destroy(currentTexture);
        }
    }

    // Geometry
    {
        auto uploadBufferChunked = [&](
            uint32_t& pendingHead,
            size_t totalCount,
            size_t elementSize,
            const void* sourceData,
            VkBuffer targetBuffer,
            VkDeviceSize targetOffset
        ) -> bool {
            if (pendingHead >= totalCount) {
                return true;
            }

            size_t remainingElements = totalCount - pendingHead;
            size_t remainingSize = remainingElements * elementSize;

            OffsetAllocator::Allocation allocation = stagingAllocator.allocate(remainingSize);
            if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                // Try to fit as much as possible in remaining space
                size_t freeSpace = stagingAllocator.storageReport().totalFreeSpace;
                size_t maxElements = freeSpace / elementSize;

                if (maxElements == 0) {
                    // Can't fit even one element - submit and retry next frame
                    assert(freeSpace < ASSET_LOAD_STAGING_BUFFER_SIZE && "NO_SPACE on empty staging buffer");
                    uploadStaging->SubmitCommandBuffer();
                    return false;
                }

                // Try partial allocation
                remainingSize = maxElements * elementSize;
                allocation = stagingAllocator.allocate(remainingSize);

                if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                    // Fragmentation - submit and retry
                    uploadStaging->SubmitCommandBuffer();
                    return false;
                }

                remainingElements = maxElements;
            }

            uploadStaging->StartCommandBuffer();

            const char* elementData = static_cast<const char*>(sourceData) + (pendingHead * elementSize);
            char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation.offset;
            memcpy(stagingPtr, elementData, remainingSize);

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = allocation.offset;
            copyRegion.dstOffset = targetOffset + (pendingHead * elementSize);
            copyRegion.size = remainingSize;

            vkCmdCopyBuffer(uploadStaging->GetCommandBuffer(), stagingBuffer.handle, targetBuffer, 1, &copyRegion);

            pendingHead += remainingElements;
            return pendingHead >= totalCount;
        };


        size_t vertexSize = rawData.bIsSkeletalModel ? sizeof(SkinnedVertex) : sizeof(Vertex);
        VkBuffer targetVertexBuffer = rawData.bIsSkeletalModel ? resourceManager->megaSkinnedVertexBuffer.handle : resourceManager->megaVertexBuffer.handle;

        if (!uploadBufferChunked(pendingVerticesHead,
                                 rawData.vertices.size(),
                                 vertexSize,
                                 rawData.vertices.data(),
                                 targetVertexBuffer,
                                 model->modelData.vertexAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }
        if (!uploadBufferChunked(pendingMeshletVerticesHead,
                                 rawData.meshletVertices.size(),
                                 sizeof(uint32_t),
                                 rawData.meshletVertices.data(),
                                 resourceManager->megaMeshletVerticesBuffer.handle,
                                 model->modelData.meshletVertexAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (!uploadBufferChunked(pendingMeshletTrianglesHead,
                                 rawData.meshletTriangles.size(),
                                 sizeof(uint8_t),
                                 rawData.meshletTriangles.data(),
                                 resourceManager->megaMeshletTrianglesBuffer.handle,
                                 model->modelData.meshletTriangleAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (!uploadBufferChunked(pendingMeshletsHead,
                                 rawData.meshlets.size(),
                                 sizeof(Meshlet),
                                 rawData.meshlets.data(),
                                 resourceManager->megaMeshletBuffer.handle,
                                 model->modelData.meshletAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (!uploadBufferChunked(pendingPrimitivesHead,
                                 rawData.primitives.size(),
                                 sizeof(MeshletPrimitive),
                                 rawData.primitives.data(),
                                 resourceManager->primitiveBuffer.handle,
                                 model->modelData.primitiveAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (pendingBufferBarrier == 0) {
            uploadStaging->StartCommandBuffer();
            std::vector<VkBufferMemoryBarrier2> releaseBarriers;
            auto createBufferBarrier = [&](VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
                VkBufferMemoryBarrier2 barrier{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                    .dstAccessMask = VK_ACCESS_2_NONE,
                    .srcQueueFamilyIndex = context->transferQueueFamily,
                    .dstQueueFamilyIndex = context->graphicsQueueFamily,
                    .buffer = buffer,
                    .offset = offset,
                    .size = size
                };
                return barrier;
            };

            releaseBarriers.push_back(createBufferBarrier(
                targetVertexBuffer,
                model->modelData.vertexAllocation.offset,
                rawData.vertices.size() * vertexSize
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->megaMeshletVerticesBuffer.handle,
                model->modelData.meshletVertexAllocation.offset,
                rawData.meshletVertices.size() * sizeof(uint32_t)
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->megaMeshletTrianglesBuffer.handle,
                model->modelData.meshletTriangleAllocation.offset,
                rawData.meshletTriangles.size() * sizeof(uint8_t)
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->megaMeshletBuffer.handle,
                model->modelData.meshletAllocation.offset,
                rawData.meshlets.size() * sizeof(Meshlet)
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->primitiveBuffer.handle,
                model->modelData.primitiveAllocation.offset,
                rawData.primitives.size() * sizeof(MeshletPrimitive)
            ));

            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.bufferMemoryBarrierCount = releaseBarriers.size();
            depInfo.pBufferMemoryBarriers = releaseBarriers.data();
            vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

            for (auto& barrier : releaseBarriers) {
                model->bufferAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));
            }

            pendingBufferBarrier = 1;
        }
    }


    if (uploadStaging->IsCommandBufferStarted()) {
        uploadStaging->SubmitCommandBuffer();
        return ThreadState::InProgress;
    }

    return ThreadState::Complete;
}

bool WillModelLoader::PostThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager)
{
    // Textures
    pendingTextures.clear();

    // Samplers (doesnt need to be created here, but whatever)
    if (!pendingSamplerInfos.empty()) {
        for (VkSamplerCreateInfo& sampler : pendingSamplerInfos) {
            model->modelData.samplers.push_back(Render::Sampler::CreateSampler(context, sampler));
        }
        pendingSamplerInfos.clear();
    }

    // Materials
    {
        // Samplers
        auto remapSamplers = [](auto& indices, const std::vector<Render::BindlessSamplerHandle>& map) {
            indices.x = indices.x >= 0 ? map[indices.x].index : 0;
            indices.y = indices.y >= 0 ? map[indices.y].index : 0;
            indices.z = indices.z >= 0 ? map[indices.z].index : 0;
            indices.w = indices.w >= 0 ? map[indices.w].index : 0;
        };

        model->modelData.samplerIndexToDescriptorBufferIndexMap.resize(model->modelData.samplers.size());
        for (int32_t i = 0; i < model->modelData.samplers.size(); ++i) {
            model->modelData.samplerIndexToDescriptorBufferIndexMap[i] = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateSampler(model->modelData.samplers[i].handle);
        }

        for (MaterialProperties& material : rawData.materials) {
            remapSamplers(material.textureSamplerIndices, model->modelData.samplerIndexToDescriptorBufferIndexMap);
            remapSamplers(material.textureSamplerIndices2, model->modelData.samplerIndexToDescriptorBufferIndexMap);
        }

        // Textures
        auto remapTextures = [](auto& indices, const std::vector<Render::BindlessTextureHandle>& map) {
            indices.x = indices.x >= 0 ? map[indices.x].index : 0;
            indices.y = indices.y >= 0 ? map[indices.y].index : 0;
            indices.z = indices.z >= 0 ? map[indices.z].index : 0;
            indices.w = indices.w >= 0 ? map[indices.w].index : 0;
        };


        model->modelData.textureIndexToDescriptorBufferIndexMap.resize(model->modelData.imageViews.size());
        for (int32_t i = 0; i < model->modelData.imageViews.size(); ++i) {
            if (model->modelData.imageViews[i].handle == VK_NULL_HANDLE) {
                model->modelData.textureIndexToDescriptorBufferIndexMap[i] = {0, 0};
                continue;
            }

            model->modelData.textureIndexToDescriptorBufferIndexMap[i] = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateTexture({
                .imageView = model->modelData.imageViews[i].handle,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
        }

        for (MaterialProperties& material : rawData.materials) {
            remapTextures(material.textureImageIndices, model->modelData.textureIndexToDescriptorBufferIndexMap);
            remapTextures(material.textureImageIndices2, model->modelData.textureIndexToDescriptorBufferIndexMap);
        }

        // todo: materials will be dynamic here, no longer uploading to staging/copy
        // Materials
        // size_t sizeMaterials = meshletModel_.materials.size() * sizeof(Render::MaterialProperties);
        // model.materialAllocation = materialBufferAllocator.allocate(sizeMaterials);
        // memcpy(static_cast<char*>(materialBuffer.allocationInfo.pMappedData) + model.materialAllocation.offset, meshletModel_.materials.data(), sizeMaterials);
    }

    return true;
}
} // AssetLoad
