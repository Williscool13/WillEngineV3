//
// Created by William on 2025-12-23.
//

#include "will_model_load_job.h"

#include "asset-load/asset_load_config.h"
#include "ktxvulkan.h"
#include "render/model/model_serialization.h"
#include "render/model/will_model_asset.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
WillModelLoadJob::WillModelLoadJob() = default;

WillModelLoadJob::WillModelLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager, VkCommandBuffer commandBuffer)
    : context(context), resourceManager(resourceManager), commandBuffer(commandBuffer)
{
    task = std::make_unique<LoadModelTask>();
}

WillModelLoadJob::~WillModelLoadJob() = default;

void WillModelLoadJob::StartJob()
{
    if (!uploadStaging) {
        uploadStaging = std::make_unique<UploadStaging>(context, commandBuffer, TEXTURE_LOAD_STAGING_SIZE);
    }
}

TaskState WillModelLoadJob::TaskExecute(enki::TaskScheduler* scheduler)
{
    if (taskState == TaskState::NotStarted) {
        task->loadJob = this;
        taskState = TaskState::InProgress;
        scheduler->AddTaskSetToPipe(task.get());
    }

    if (task->GetIsComplete()) {
        return taskState;
    }

    return TaskState::InProgress;
}

bool WillModelLoadJob::PreThreadExecute()
{
    if (!outputModel) {
        return false;
    }

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

    outputModel->modelData.bIsSkinned = rawData.bIsSkeletalModel;
    outputModel->modelData.vertexAllocation = selectedAllocator->allocate(sizeVertices);
    if (outputModel->modelData.vertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega vertex buffer to upload {}", outputModel->name);
        return false;
    }

    size_t sizeMeshletVertices = rawData.meshletVertices.size() * sizeof(uint32_t);
    outputModel->modelData.meshletVertexAllocation = resourceManager->meshletVertexBufferAllocator.allocate(sizeMeshletVertices);
    if (outputModel->modelData.meshletVertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(outputModel->modelData.vertexAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega meshlet vertex buffer to upload {}", outputModel->name);
        return false;
    }

    size_t sizeMeshletTriangles = rawData.meshletTriangles.size() / 3 * sizeof(uint32_t);
    outputModel->modelData.meshletTriangleAllocation = resourceManager->meshletTriangleBufferAllocator.allocate(sizeMeshletTriangles);
    if (outputModel->modelData.meshletTriangleAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(outputModel->modelData.vertexAllocation);
        resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega meshlet triangle buffer to upload {}", outputModel->name);
        return false;
    }

    size_t sizeMeshlets = rawData.meshlets.size() * sizeof(Meshlet);
    outputModel->modelData.meshletAllocation = resourceManager->meshletBufferAllocator.allocate(sizeMeshlets);
    if (outputModel->modelData.meshletAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(outputModel->modelData.vertexAllocation);
        resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
        resourceManager->meshletTriangleBufferAllocator.free(outputModel->modelData.meshletTriangleAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega meshlet buffer to upload {}", outputModel->name);
        return false;
    }

    size_t sizePrimitives = rawData.primitives.size() * sizeof(MeshletPrimitive);
    outputModel->modelData.primitiveAllocation = resourceManager->primitiveBufferAllocator.allocate(sizePrimitives);
    if (outputModel->modelData.primitiveAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
        selectedAllocator->free(outputModel->modelData.vertexAllocation);
        resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
        resourceManager->meshletTriangleBufferAllocator.free(outputModel->modelData.meshletTriangleAllocation);
        resourceManager->meshletBufferAllocator.free(outputModel->modelData.meshletAllocation);
        SPDLOG_ERROR("[WillModelLoader::PreThreadExecute] Not enough space in mega primitive buffer to upload {}", outputModel->name);
        return false;
    }

    uint32_t vertexOffset = outputModel->modelData.vertexAllocation.offset / (rawData.bIsSkeletalModel ? sizeof(SkinnedVertex) : sizeof(Vertex));
    uint32_t meshletVerticesOffset = outputModel->modelData.meshletVertexAllocation.offset / sizeof(uint32_t);
    uint32_t meshletTriangleOffset = outputModel->modelData.meshletTriangleAllocation.offset / sizeof(uint32_t);

    for (Meshlet& meshlet : rawData.meshlets) {
        meshlet.vertexOffset += vertexOffset;
        meshlet.meshletVertexOffset += meshletVerticesOffset;
        meshlet.meshletTriangleOffset = meshlet.meshletTriangleOffset / 3 + meshletTriangleOffset;
    }

    uint32_t meshletOffset = outputModel->modelData.meshletAllocation.offset / sizeof(Meshlet);
    for (auto& primitive : rawData.primitives) {
        primitive.meshletOffset += meshletOffset;
    }

    uint32_t primitiveOffsetCount = outputModel->modelData.primitiveAllocation.offset / sizeof(MeshletPrimitive);
    for (auto& mesh : rawData.allMeshes) {
        for (auto& primitiveIndex : mesh.primitiveProperties) {
            primitiveIndex.index += primitiveOffsetCount;
        }
    }

    outputModel->modelData.meshes = std::move(rawData.allMeshes);
    outputModel->modelData.nodes = std::move(rawData.nodes);
    outputModel->modelData.inverseBindMatrices = std::move(rawData.inverseBindMatrices);
    outputModel->modelData.animations = std::move(rawData.animations);
    outputModel->modelData.materials = std::move(rawData.materials);

    // Convert SkinnedVertex to Vertex
    convertedVertices.reserve(rawData.vertices.size());
    for (const auto& skinnedVert : rawData.vertices) {
        Vertex v{};
        v.position = skinnedVert.position;
        v.normal = skinnedVert.normal;
        v.tangent = skinnedVert.tangent;
        v.texcoordU = skinnedVert.texcoordU;
        v.texcoordV = skinnedVert.texcoordV;
        v.color = skinnedVert.color;
        convertedVertices.push_back(v);
    }

    // Pack triangle into uint32_t (1x uint8 padding). Better access pattern on GPU
    packedTriangles.reserve(rawData.meshletTriangles.size() / 3);

    for (size_t i = 0; i < rawData.meshletTriangles.size(); i += 3) {
        uint32_t packed = rawData.meshletTriangles[i + 0] |
                          (rawData.meshletTriangles[i + 1] << 8) |
                          (rawData.meshletTriangles[i + 2] << 16);
        packedTriangles.push_back(packed);
    }

    for (auto currentTexture : pendingTextures) {
        if (currentTexture == nullptr) {
            outputModel->modelData.images.emplace_back();
            outputModel->modelData.imageViews.emplace_back();
        }
        else {
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
            outputModel->modelData.images.push_back(std::move(allocatedImage));
            outputModel->modelData.imageViews.push_back(std::move(imageView));
        }
    }

    return true;
}

ThreadState WillModelLoadJob::ThreadExecute()
{
    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
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
                continue;
            }

            Render::AllocatedImage& image = outputModel->modelData.images[pendingTextureHead];
            Render::ImageView& imageView = outputModel->modelData.imageViews[pendingTextureHead];

            uploadStaging->StartCommandBuffer();
            if (bPendingPreCopyBarrier) {
                VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
                    image.handle,
                    Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, currentTexture->numLevels, 0, currentTexture->numLayers),
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                );

                VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = 1;
                depInfo.pImageMemoryBarriers = &preCopyBarrier;
                vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

                bPendingPreCopyBarrier = false;
            }

            for (; pendingMipHead < currentTexture->numLevels; pendingMipHead++) {
                ZoneScopedN("Upload Mip");
                size_t mipOffset;
                ktxTexture_GetImageOffset(ktxTexture(currentTexture), pendingMipHead, 0, 0, &mipOffset);
                uint32_t mipWidth = std::max(1u, currentTexture->baseWidth >> pendingMipHead);
                uint32_t mipHeight = std::max(1u, currentTexture->baseHeight >> pendingMipHead);
                uint32_t mipDepth = std::max(1u, currentTexture->baseDepth >> pendingMipHead);
                size_t mipSize = ktxTexture_GetImageSize(ktxTexture(currentTexture), pendingMipHead);

                size_t allocation = stagingAllocator.Allocate(mipSize);
                if (allocation == SIZE_MAX) {
                    uploadStaging->SubmitCommandBuffer();
                    uploadCount++;
                    return ThreadState::InProgress;
                }

                char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
                memcpy(stagingPtr, currentTexture->pData + mipOffset, mipSize);

                VkBufferImageCopy copyRegion{};
                copyRegion.bufferOffset = allocation;
                copyRegion.bufferRowLength = 0;
                copyRegion.bufferImageHeight = 0;
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = pendingMipHead;
                copyRegion.imageSubresource.baseArrayLayer = 0;
                copyRegion.imageSubresource.layerCount = currentTexture->numLayers;
                copyRegion.imageOffset = {0, 0, 0};
                copyRegion.imageExtent = {mipWidth, mipHeight, mipDepth};

                vkCmdCopyBufferToImage(
                    uploadStaging->GetCommandBuffer(),
                    stagingBuffer.handle,
                    image.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion
                );
            }

            if (bPendingFinalBarrier) {
                VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
                    image.handle,
                    Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, currentTexture->numLevels, 0, currentTexture->numLayers),
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                );
                finalBarrier.srcQueueFamilyIndex = context->transferQueueFamily;
                finalBarrier.dstQueueFamilyIndex = context->graphicsQueueFamily;

                VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = 1;
                depInfo.pImageMemoryBarriers = &finalBarrier;
                vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

                // Image acquires that needs to be executed by render thread
                outputModel->imageAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(finalBarrier));
                bPendingFinalBarrier = false;
            }

            ktxTexture2_Destroy(currentTexture);
            pendingMipHead = 0;
            bPendingPreCopyBarrier = true;
            bPendingFinalBarrier = true;
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

            uploadStaging->StartCommandBuffer();
            size_t remainingElements = totalCount - pendingHead;
            size_t remainingSize = remainingElements * elementSize;

            size_t allocation = stagingAllocator.Allocate(remainingSize);

            // Can't fit entire data, will try to upload in chunks. Start by trying to fit as much as possible in remaining space
            if (allocation == SIZE_MAX) {
                size_t freeSpace = stagingAllocator.GetRemaining();
                size_t maxElements = freeSpace / elementSize;

                // Can't fit even one element - submit and retry next frame
                if (maxElements == 0) {
                    assert(freeSpace < WILL_MODEL_LOAD_STAGING_SIZE && "NO_SPACE on empty staging buffer");
                    uploadStaging->SubmitCommandBuffer();
                    uploadCount++;
                    return false;
                }

                remainingSize = maxElements * elementSize;
                allocation = stagingAllocator.Allocate(remainingSize);
                assert(allocation != SIZE_MAX && "Allocation of remaining memory failed even though there should have been enough space");

                const char* elementData = static_cast<const char*>(sourceData) + (pendingHead * elementSize);
                char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
                memcpy(stagingPtr, elementData, remainingSize);
                VkBufferCopy copyRegion{};
                copyRegion.srcOffset = allocation;
                copyRegion.dstOffset = targetOffset + (pendingHead * elementSize);
                copyRegion.size = remainingSize;
                vkCmdCopyBuffer(uploadStaging->GetCommandBuffer(), stagingBuffer.handle, targetBuffer, 1, &copyRegion);
                uploadStaging->SubmitCommandBuffer();
                uploadCount++;
                pendingHead += maxElements;
                return false;
            }

            const char* elementData = static_cast<const char*>(sourceData) + (pendingHead * elementSize);
            char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
            memcpy(stagingPtr, elementData, remainingSize);

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = allocation;
            copyRegion.dstOffset = targetOffset + (pendingHead * elementSize);
            copyRegion.size = remainingSize;

            vkCmdCopyBuffer(uploadStaging->GetCommandBuffer(), stagingBuffer.handle, targetBuffer, 1, &copyRegion);

            pendingHead += remainingElements;
            return pendingHead >= totalCount;
        };


        size_t vertexSize = rawData.bIsSkeletalModel ? sizeof(SkinnedVertex) : sizeof(Vertex);
        VkBuffer targetVertexBuffer = rawData.bIsSkeletalModel ? resourceManager->megaSkinnedVertexBuffer.handle : resourceManager->megaVertexBuffer.handle;
        const void* vertexDataPtr;
        if (rawData.bIsSkeletalModel) {
            vertexDataPtr = rawData.vertices.data();
        }
        else {
            vertexDataPtr = convertedVertices.data();
        }

        if (!uploadBufferChunked(pendingVerticesHead,
                                 rawData.vertices.size(),
                                 vertexSize,
                                 vertexDataPtr,
                                 targetVertexBuffer,
                                 outputModel->modelData.vertexAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (!uploadBufferChunked(pendingMeshletVerticesHead,
                                 rawData.meshletVertices.size(),
                                 sizeof(uint32_t),
                                 rawData.meshletVertices.data(),
                                 resourceManager->megaMeshletVerticesBuffer.handle,
                                 outputModel->modelData.meshletVertexAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (!uploadBufferChunked(pendingMeshletTrianglesHead,
                                 packedTriangles.size(),
                                 sizeof(uint32_t),
                                 packedTriangles.data(),
                                 resourceManager->megaMeshletTrianglesBuffer.handle,
                                 outputModel->modelData.meshletTriangleAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (!uploadBufferChunked(pendingMeshletsHead,
                                 rawData.meshlets.size(),
                                 sizeof(Meshlet),
                                 rawData.meshlets.data(),
                                 resourceManager->megaMeshletBuffer.handle,
                                 outputModel->modelData.meshletAllocation.offset)
        ) {
            return ThreadState::InProgress;
        }

        if (!uploadBufferChunked(pendingPrimitivesHead,
                                 rawData.primitives.size(),
                                 sizeof(MeshletPrimitive),
                                 rawData.primitives.data(),
                                 resourceManager->primitiveBuffer.handle,
                                 outputModel->modelData.primitiveAllocation.offset)
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
                if (context->bMaintenance9Enabled) {
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                }
                return barrier;
            };

            releaseBarriers.push_back(createBufferBarrier(
                targetVertexBuffer,
                outputModel->modelData.vertexAllocation.offset,
                rawData.vertices.size() * vertexSize
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->megaMeshletVerticesBuffer.handle,
                outputModel->modelData.meshletVertexAllocation.offset,
                rawData.meshletVertices.size() * sizeof(uint32_t)
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->megaMeshletTrianglesBuffer.handle,
                outputModel->modelData.meshletTriangleAllocation.offset,
                rawData.meshletTriangles.size() * sizeof(uint32_t)
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->megaMeshletBuffer.handle,
                outputModel->modelData.meshletAllocation.offset,
                rawData.meshlets.size() * sizeof(Meshlet)
            ));

            releaseBarriers.push_back(createBufferBarrier(
                resourceManager->primitiveBuffer.handle,
                outputModel->modelData.primitiveAllocation.offset,
                rawData.primitives.size() * sizeof(MeshletPrimitive)
            ));

            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.bufferMemoryBarrierCount = releaseBarriers.size();
            depInfo.pBufferMemoryBarriers = releaseBarriers.data();
            vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

            for (auto& barrier : releaseBarriers) {
                outputModel->bufferAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));
            }

            pendingBufferBarrier = 1;
        }
    }


    if (uploadStaging->IsCommandBufferStarted()) {
        uploadStaging->SubmitCommandBuffer();
        uploadCount++;
        return ThreadState::InProgress;
    }

    return ThreadState::Complete;
}

bool WillModelLoadJob::PostThreadExecute()
{
    pendingTextures.clear();

    // Materials
    {
        // Samplers
        auto remapSamplers = [](auto& indices, const std::vector<Render::BindlessSamplerHandle>& map) {
            indices.x = indices.x >= 0 ? map[indices.x].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
            indices.y = indices.y >= 0 ? map[indices.y].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
            indices.z = indices.z >= 0 ? map[indices.z].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
            indices.w = indices.w >= 0 ? map[indices.w].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
        };

        // todo: sampler can be hashed since there aren't that many of them. Then the entire engine won't have more than ~20 samplers, just need to find the right index w/ hash
        outputModel->modelData.samplerIndexToDescriptorBufferIndexMap.resize(outputModel->modelData.samplers.size());
        for (int32_t i = 0; i < outputModel->modelData.samplers.size(); ++i) {
            outputModel->modelData.samplerIndexToDescriptorBufferIndexMap[i] = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateSampler(outputModel->modelData.samplers[i].handle);
        }

        for (MaterialProperties& material : outputModel->modelData.materials) {
            remapSamplers(material.textureSamplerIndices, outputModel->modelData.samplerIndexToDescriptorBufferIndexMap);
            remapSamplers(material.textureSamplerIndices2, outputModel->modelData.samplerIndexToDescriptorBufferIndexMap);
        }

        // Textures
        auto remapTextures = [](auto& indices, const std::vector<Render::BindlessTextureHandle>& map) {
            indices.x = indices.x >= 0 ? map[indices.x].index : WHITE_IMAGE_BINDLESS_INDEX;
            indices.y = indices.y >= 0 ? map[indices.y].index : WHITE_IMAGE_BINDLESS_INDEX;
            indices.z = indices.z >= 0 ? map[indices.z].index : WHITE_IMAGE_BINDLESS_INDEX;
            indices.w = indices.w >= 0 ? map[indices.w].index : WHITE_IMAGE_BINDLESS_INDEX;
        };


        outputModel->modelData.textureIndexToDescriptorBufferIndexMap.resize(outputModel->modelData.imageViews.size());
        for (int32_t i = 0; i < outputModel->modelData.imageViews.size(); ++i) {
            if (outputModel->modelData.imageViews[i].handle == VK_NULL_HANDLE) {
                outputModel->modelData.textureIndexToDescriptorBufferIndexMap[i] = {ERROR_IMAGE_BINDLESS_INDEX, 0};
                continue;
            }

            outputModel->modelData.textureIndexToDescriptorBufferIndexMap[i] = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateTexture({
                .imageView = outputModel->modelData.imageViews[i].handle,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
        }

        for (MaterialProperties& material : outputModel->modelData.materials) {
            remapTextures(material.textureImageIndices, outputModel->modelData.textureIndexToDescriptorBufferIndexMap);
            remapTextures(material.textureImageIndices2, outputModel->modelData.textureIndexToDescriptorBufferIndexMap);
        }
    }

    return true;
}

uint32_t WillModelLoadJob::GetUploadCount()
{
    return uploadCount;
}

void WillModelLoadJob::Reset()
{
    rawData.Reset();
    taskState = TaskState::NotStarted;
    willModelHandle = Engine::WillModelHandle::INVALID;
    outputModel = nullptr;
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
    convertedVertices.clear();
    packedTriangles.clear();
    uploadCount = 0;
}

void WillModelLoadJob::LoadModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
{
    ZoneScopedN("LoadModelTask");
    if (loadJob == nullptr) {
        return;
    } {
        ZoneScopedN("FileExistsCheck");
        if (!std::filesystem::exists(loadJob->outputModel->source)) {
            SPDLOG_ERROR("Failed to find path to willmodel - {}", loadJob->outputModel->name);
            loadJob->taskState = TaskState::Failed;
            return;
        }
    }

    Render::ModelReader reader = Render::ModelReader(loadJob->outputModel->source);

    if (!reader.GetSuccessfullyLoaded()) {
        SPDLOG_ERROR("Failed to load willmodel - {}", loadJob->outputModel->name);
        loadJob->taskState = TaskState::Failed;
        return;
    }

    std::vector<uint8_t> modelBinData; {
        ZoneScopedN("ReadModelBin");
        modelBinData = reader.ReadFile("model.bin");
    }

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

    const uint8_t* dataPtr = modelBinData.data() + offset; {
        ZoneScopedN("ParseGeometryData");
        loadJob->rawData.bIsSkeletalModel = header->bIsSkeletalModel;
        readArray(loadJob->rawData.vertices, header->vertexCount);
        readArray(loadJob->rawData.meshletVertices, header->meshletVertexCount);
        readArray(loadJob->rawData.meshletTriangles, header->meshletTriangleCount);
        readArray(loadJob->rawData.meshlets, header->meshletCount);
        readArray(loadJob->rawData.primitives, header->primitiveCount);
        readArray(loadJob->rawData.materials, header->materialCount);
    }

    dataPtr = modelBinData.data() + offset; {
        ZoneScopedN("ParseMeshes");
        loadJob->rawData.allMeshes.resize(header->meshCount);
        for (uint32_t i = 0; i < header->meshCount; i++) {
            ReadMeshInformation(dataPtr, loadJob->rawData.allMeshes[i]);
        }
    } {
        ZoneScopedN("ParseNodes");
        loadJob->rawData.nodes.resize(header->nodeCount);
        for (uint32_t i = 0; i < header->nodeCount; i++) {
            ReadNode(dataPtr, loadJob->rawData.nodes[i]);
        }
    } {
        ZoneScopedN("ParseAnimations");
        loadJob->rawData.animations.resize(header->animationCount);
        for (uint32_t i = 0; i < header->animationCount; i++) {
            ReadAnimation(dataPtr, loadJob->rawData.animations[i]);
        }
    }

    offset = dataPtr - modelBinData.data(); {
        ZoneScopedN("ParseSkeletalData");
        readArray(loadJob->rawData.inverseBindMatrices, header->inverseBindMatrixCount);
    } {
        ZoneScopedN("CreateSamplers");
        std::vector<VkSamplerCreateInfo> samplerInfos{};
        readArray(samplerInfos, header->samplerCount);
        for (VkSamplerCreateInfo& sampler : samplerInfos) {
            loadJob->outputModel->modelData.samplers.push_back(Render::Sampler::CreateSampler(loadJob->context, sampler));
        }
    }

    //
    {
        ZoneScopedN("LoadTextures");
        for (int i = 0; i < header->textureCount; ++i) {
            ZoneScopedN("LoadSingleTexture");

            std::string textureName = fmt::format("textures/texture_{}.ktx2", i);
            if (!reader.HasFile(textureName)) {
                SPDLOG_ERROR("[WillModelLoader::TaskImplementation] Failed to find texture {}", textureName);
                loadJob->pendingTextures.push_back(nullptr);
                continue;
            }

            ktxTexture2* loadedTexture = nullptr;
            ktx_error_code_e result;
            std::vector<uint8_t> ktxData;

            //
            {
                ZoneScopedN("CreateKtxTexture")
                //
                {
                    ZoneScopedN("ReadKTXFile");
                    ktxData = reader.ReadFile(textureName);
                }
                //
                {
                    ZoneScopedN("KTXCreateFromMemory");
                    result = ktxTexture2_CreateFromMemory(ktxData.data(), ktxData.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &loadedTexture);
                    if (result != KTX_SUCCESS) {
                        SPDLOG_ERROR("[TextureLoadJob] Failed to load KTX texture: {}", textureName);
                        loadJob->taskState = TaskState::Failed;
                        return;
                    }
                }
            }

            assert(!ktxTexture2_NeedsTranscoding(loadedTexture) && "This engine no longer supports UASTC/ETC1S compressed textures");

            // Size check
            ktx_size_t mip0Size = ktxTexture_GetImageSize(ktxTexture(loadedTexture), 0);
            if (mip0Size > WILL_MODEL_LOAD_STAGING_SIZE) {
                SPDLOG_WARN("Texture too big to fit in the staging buffer for texture {}, pruning", textureName);
                loadJob->pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            // Texture dimension and array check
            if (loadedTexture->numDimensions != 2) {
                SPDLOG_WARN("Engine does not support non 2D image textures {}, pruning", textureName);
                loadJob->pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            if (loadedTexture->isArray) {
                SPDLOG_WARN("Engine does not support texture arrays {}, pruning", textureName);
                loadJob->pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            if (loadedTexture->isCubemap) {
                SPDLOG_WARN("Texture does not support cubemaps {}, pruning", textureName);
                loadJob->pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            loadJob->pendingTextures.push_back(loadedTexture);
        }
    }

    loadJob->rawData.name = "Loaded Model";
    loadJob->taskState = TaskState::Complete;
}
} // AssetLoad
