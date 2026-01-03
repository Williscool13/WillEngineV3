//
// Created by William on 2025-12-23.
//

#include "will_model_load_job.h"

#include "asset_load_config.h"
#include "ktxvulkan.h"
#include "tiny_gltf.h"
#include "editor/asset-generation/asset_generation_types.h"
#include "editor/asset-generation/asset_generation_types.h"
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


    return true;
}

ThreadState WillModelLoadJob::ThreadExecute()
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
                outputModel->modelData.images.emplace_back();
                outputModel->modelData.imageViews.emplace_back();
                continue;
            }

            ktx_size_t dataSize = currentTexture->dataSize;
            OffsetAllocator::Allocation allocation = stagingAllocator.allocate(dataSize);
            if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                assert(stagingAllocator.storageReport().totalFreeSpace != WILL_MODEL_LOAD_STAGING_SIZE);
                uploadStaging->SubmitCommandBuffer();
                uploadCount++;
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

            // todo: upload each mip individually for particularly high quality textures.
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
            outputModel->imageAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));

            outputModel->modelData.images.push_back(std::move(allocatedImage));
            outputModel->modelData.imageViews.push_back(std::move(imageView));
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

            uploadStaging->StartCommandBuffer();
            size_t remainingElements = totalCount - pendingHead;
            size_t remainingSize = remainingElements * elementSize;

            OffsetAllocator::Allocation allocation = stagingAllocator.allocate(remainingSize);

            // Can't fit entire data, will try to upload in chunks. Start by trying to fit as much as possible in remaining space
            if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                size_t freeSpace = stagingAllocator.storageReport().totalFreeSpace;
                size_t maxElements = freeSpace / elementSize;

                // Can't fit even one element - submit and retry next frame
                if (maxElements == 0) {
                    assert(freeSpace < WILL_MODEL_LOAD_STAGING_SIZE && "NO_SPACE on empty staging buffer");
                    uploadStaging->SubmitCommandBuffer();
                    uploadCount++;
                    return false;
                }

                // Try partial allocation
                remainingSize = maxElements * elementSize;
                allocation = stagingAllocator.allocate(remainingSize);

                // Still can't fit. Likely caused by fragmentation - submit and retry
                if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
                    uploadStaging->SubmitCommandBuffer();
                    uploadCount++;
                    return false;
                }

                const char* elementData = static_cast<const char*>(sourceData) + (pendingHead * elementSize);
                char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation.offset;
                memcpy(stagingPtr, elementData, remainingSize);
                VkBufferCopy copyRegion{};
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = targetOffset + (pendingHead * elementSize);
                copyRegion.size = remainingSize;
                vkCmdCopyBuffer(uploadStaging->GetCommandBuffer(), stagingBuffer.handle, targetBuffer, 1, &copyRegion);
                uploadStaging->SubmitCommandBuffer();
                uploadCount++;
                pendingHead += maxElements;
                return false;
            }

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

glm::vec4 WillModelLoadJob::GenerateBoundingSphere(std::span<Vertex> vertices)
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

glm::vec4 WillModelLoadJob::GenerateBoundingSphere(std::span<SkinnedVertex> vertices)
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

void WillModelLoadJob::LoadModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
{
    ZoneScopedN("LoadModelTask");

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(StubLoadImageData, nullptr);
    loader.SetImageWriter(StubWriteImageData, nullptr);
    std::string err, warn;

    if (loadJob == nullptr) {
        return;
    }

    //
    {
        ZoneScopedN("FileExistsCheck");
        if (!std::filesystem::exists(loadJob->outputModel->source)) {
            SPDLOG_ERROR("Failed to find path to model - {}", loadJob->outputModel->name);
            loadJob->taskState = TaskState::Failed;
            return;
        }
    }

    bool ret;
    //
    {
        ZoneScopedN("LoadGLTF");
        if (loadJob->outputModel->source.extension() == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, loadJob->outputModel->source.string());
        }
        else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, loadJob->outputModel->source.string());
        }
    }

    if (!warn.empty()) {
        SPDLOG_WARN("GLTF Warning: {}", warn);
    }

    if (!err.empty()) {
        SPDLOG_ERROR("GLTF Error: {}", err);
    }

    if (!ret) {
        SPDLOG_ERROR("Failed to load GLTF model - {}", loadJob->outputModel->name);
        loadJob->taskState = TaskState::Failed;
        return;
    }

    //
    {
        ZoneScopedN("ParseMaterials");
        loadJob->rawData.materials.reserve(model.materials.size());
        for (const auto& gltfMaterial : model.materials) {
            MaterialProperties material = {};

            material.colorFactor = glm::vec4(
                gltfMaterial.pbrMetallicRoughness.baseColorFactor[0],
                gltfMaterial.pbrMetallicRoughness.baseColorFactor[1],
                gltfMaterial.pbrMetallicRoughness.baseColorFactor[2],
                gltfMaterial.pbrMetallicRoughness.baseColorFactor[3]
            );

            material.metalRoughFactors.x = gltfMaterial.pbrMetallicRoughness.metallicFactor;
            material.metalRoughFactors.y = gltfMaterial.pbrMetallicRoughness.roughnessFactor;

            material.alphaProperties.x = gltfMaterial.alphaCutoff;
            material.alphaProperties.z = gltfMaterial.doubleSided ? 1.0f : 0.0f;

            if (gltfMaterial.alphaMode == "OPAQUE") {
                material.alphaProperties.y = static_cast<float>(Render::MaterialType::SOLID);
            }
            else if (gltfMaterial.alphaMode == "BLEND") {
                material.alphaProperties.y = static_cast<float>(Render::MaterialType::BLEND);
            }
            else if (gltfMaterial.alphaMode == "MASK") {
                material.alphaProperties.y = static_cast<float>(Render::MaterialType::CUTOUT);
            }

            material.emissiveFactor = glm::vec4(
                gltfMaterial.emissiveFactor[0],
                gltfMaterial.emissiveFactor[1],
                gltfMaterial.emissiveFactor[2],
                1.0f
            );

            material.textureImageIndices = glm::ivec4(-1);
            material.textureSamplerIndices = glm::ivec4(-1);
            material.textureImageIndices2 = glm::ivec4(-1);
            material.textureSamplerIndices2 = glm::ivec4(-1);

            if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                const auto& tex = model.textures[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index];
                material.textureImageIndices.x = tex.source;
                material.textureSamplerIndices.x = tex.sampler;
            }

            if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
                const auto& tex = model.textures[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index];
                material.textureImageIndices.y = tex.source;
                material.textureSamplerIndices.y = tex.sampler;
            }

            if (gltfMaterial.normalTexture.index >= 0) {
                const auto& tex = model.textures[gltfMaterial.normalTexture.index];
                material.textureImageIndices.z = tex.source;
                material.textureSamplerIndices.z = tex.sampler;
                material.physicalProperties.z = gltfMaterial.normalTexture.scale;
            }

            if (gltfMaterial.emissiveTexture.index >= 0) {
                const auto& tex = model.textures[gltfMaterial.emissiveTexture.index];
                material.textureImageIndices.w = tex.source;
                material.textureSamplerIndices.w = tex.sampler;
            }

            if (gltfMaterial.occlusionTexture.index >= 0) {
                const auto& tex = model.textures[gltfMaterial.occlusionTexture.index];
                material.textureImageIndices2.x = tex.source;
                material.textureSamplerIndices2.x = tex.sampler;
                material.physicalProperties.w = gltfMaterial.occlusionTexture.strength;
            }

            loadJob->rawData.materials.push_back(material);
        }
    }

    //
    {
        ZoneScopedN("ParseGeometryData");

        // Extract meshlet data from model extras
        if (model.extras.Has("meshletBufferView")) {
            int meshletViewIdx = model.extras.Get("meshletBufferView").GetNumberAsInt();
            int vertexIndirectionViewIdx = model.extras.Get("vertexIndirectionBufferView").GetNumberAsInt();
            int triangleViewIdx = model.extras.Get("triangleBufferView").GetNumberAsInt();

            const auto& meshletView = model.bufferViews[meshletViewIdx];
            const auto& meshletBuffer = model.buffers[meshletView.buffer];
            const uint8_t* meshletData = meshletBuffer.data.data() + meshletView.byteOffset;
            loadJob->rawData.meshlets.resize(meshletView.byteLength / sizeof(Meshlet));
            std::memcpy(loadJob->rawData.meshlets.data(), meshletData, meshletView.byteLength);

            const auto& vertexIndirectionView = model.bufferViews[vertexIndirectionViewIdx];
            const auto& vertexIndirectionBuffer = model.buffers[vertexIndirectionView.buffer];
            const uint8_t* vertexIndirectionData = vertexIndirectionBuffer.data.data() + vertexIndirectionView.byteOffset;
            loadJob->rawData.meshletVertices.resize(vertexIndirectionView.byteLength / sizeof(uint32_t));
            std::memcpy(loadJob->rawData.meshletVertices.data(), vertexIndirectionData, vertexIndirectionView.byteLength);

            const auto& triangleView = model.bufferViews[triangleViewIdx];
            const auto& triangleBuffer = model.buffers[triangleView.buffer];
            const uint8_t* triangleData = triangleBuffer.data.data() + triangleView.byteOffset;
            loadJob->rawData.meshletTriangles.resize(triangleView.byteLength);
            std::memcpy(loadJob->rawData.meshletTriangles.data(), triangleData, triangleView.byteLength);
        }

        for (const auto& mesh : model.meshes) {
            Render::MeshInformation meshInfo;
            meshInfo.name = mesh.name;

            for (const auto& primitive : mesh.primitives) {
                MeshletPrimitive primData;

                if (primitive.extras.Has("meshletOffset")) {
                    primData.meshletOffset = primitive.extras.Get("meshletOffset").GetNumberAsInt();
                    primData.meshletCount = primitive.extras.Get("meshletCount").GetNumberAsInt();
                }

                int materialIndex = -1;
                if (primitive.material >= 0) {
                    materialIndex = primitive.material;
                    primData.bHasTransparent = (static_cast<Render::MaterialType>(loadJob->rawData.materials[materialIndex].alphaProperties.y) == Render::MaterialType::BLEND);
                }

                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) {
                    SPDLOG_ERROR("Primitive missing POSITION attribute");
                    continue;
                }

                const auto& posAccessor = model.accessors[posIt->second];
                const auto& posBufferView = model.bufferViews[posAccessor.bufferView];
                const auto& posBuffer = model.buffers[posBufferView.buffer];

                const float* posData = reinterpret_cast<const float*>(
                    posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset
                );

                size_t vertexStart = loadJob->rawData.vertices.size();
                loadJob->rawData.vertices.resize(vertexStart + posAccessor.count);

                for (size_t i = 0; i < posAccessor.count; ++i) {
                    loadJob->rawData.vertices[vertexStart + i].position = glm::vec3(
                        posData[i * 3], posData[i * 3 + 1], posData[i * 3 + 2]
                    );
                    loadJob->rawData.vertices[vertexStart + i].color = glm::vec4(1.0f);
                    loadJob->rawData.vertices[vertexStart + i].normal = glm::vec3(0.0f, 0.0f, 1.0f);
                    loadJob->rawData.vertices[vertexStart + i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    loadJob->rawData.vertices[vertexStart + i].texcoordU = 0.0f;
                    loadJob->rawData.vertices[vertexStart + i].texcoordV = 0.0f;
                }

                // NORMALS
                auto normalIt = primitive.attributes.find("NORMAL");
                if (normalIt != primitive.attributes.end()) {
                    const auto& normalAccessor = model.accessors[normalIt->second];
                    const auto& normalBufferView = model.bufferViews[normalAccessor.bufferView];
                    const auto& normalBuffer = model.buffers[normalBufferView.buffer];

                    const float* normalData = reinterpret_cast<const float*>(
                        normalBuffer.data.data() + normalBufferView.byteOffset + normalAccessor.byteOffset
                    );

                    for (size_t i = 0; i < normalAccessor.count; ++i) {
                        loadJob->rawData.vertices[vertexStart + i].normal = glm::vec3(
                            normalData[i * 3], normalData[i * 3 + 1], normalData[i * 3 + 2]
                        );
                    }
                }

                // TANGENTS
                auto tangentIt = primitive.attributes.find("TANGENT");
                if (tangentIt != primitive.attributes.end()) {
                    const auto& tangentAccessor = model.accessors[tangentIt->second];
                    const auto& tangentBufferView = model.bufferViews[tangentAccessor.bufferView];
                    const auto& tangentBuffer = model.buffers[tangentBufferView.buffer];

                    const float* tangentData = reinterpret_cast<const float*>(
                        tangentBuffer.data.data() + tangentBufferView.byteOffset + tangentAccessor.byteOffset
                    );

                    for (size_t i = 0; i < tangentAccessor.count; ++i) {
                        loadJob->rawData.vertices[vertexStart + i].tangent = glm::vec4(
                            tangentData[i * 4], tangentData[i * 4 + 1], tangentData[i * 4 + 2], tangentData[i * 4 + 3]
                        );
                    }
                }

                // JOINTS_0
                bool hasJoints = false;
                auto jointsIt = primitive.attributes.find("JOINTS_0");
                if (jointsIt != primitive.attributes.end()) {
                    const auto& jointsAccessor = model.accessors[jointsIt->second];
                    const auto& jointsBufferView = model.bufferViews[jointsAccessor.bufferView];
                    const auto& jointsBuffer = model.buffers[jointsBufferView.buffer];

                    const uint8_t* jointsData = jointsBuffer.data.data() + jointsBufferView.byteOffset + jointsAccessor.byteOffset;

                    for (size_t i = 0; i < jointsAccessor.count; ++i) {
                        if (jointsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                            const uint16_t* joints = reinterpret_cast<const uint16_t*>(jointsData) + i * 4;
                            loadJob->rawData.vertices[vertexStart + i].joints = glm::uvec4(joints[0], joints[1], joints[2], joints[3]);
                        }
                        else if (jointsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                            loadJob->rawData.vertices[vertexStart + i].joints = glm::uvec4(
                                jointsData[i * 4], jointsData[i * 4 + 1], jointsData[i * 4 + 2], jointsData[i * 4 + 3]
                            );
                        }
                    }
                    hasJoints = true;
                }

                // WEIGHTS_0
                bool hasWeights = false;
                auto weightsIt = primitive.attributes.find("WEIGHTS_0");
                if (weightsIt != primitive.attributes.end()) {
                    const auto& weightsAccessor = model.accessors[weightsIt->second];
                    const auto& weightsBufferView = model.bufferViews[weightsAccessor.bufferView];
                    const auto& weightsBuffer = model.buffers[weightsBufferView.buffer];

                    const float* weightsData = reinterpret_cast<const float*>(
                        weightsBuffer.data.data() + weightsBufferView.byteOffset + weightsAccessor.byteOffset
                    );

                    for (size_t i = 0; i < weightsAccessor.count; ++i) {
                        loadJob->rawData.vertices[vertexStart + i].weights = glm::vec4(
                            weightsData[i * 4], weightsData[i * 4 + 1], weightsData[i * 4 + 2], weightsData[i * 4 + 3]
                        );
                    }
                    hasWeights = true;
                }

                static bool hasSkinned = false;
                static bool hasStatic = false;
                if (hasJoints && hasWeights) {
                    hasSkinned = true;
                }
                else {
                    hasStatic = true;
                }

                if (hasSkinned && hasStatic) {
                    SPDLOG_ERROR("Model contains mixed skinned and static meshes. Split into separate files.");
                    loadJob->taskState = TaskState::Failed;
                    return;
                }

                // TEXCOORD_0 (UV)
                auto uvIt = primitive.attributes.find("TEXCOORD_0");
                if (uvIt != primitive.attributes.end()) {
                    const auto& uvAccessor = model.accessors[uvIt->second];
                    const auto& uvBufferView = model.bufferViews[uvAccessor.bufferView];
                    const auto& uvBuffer = model.buffers[uvBufferView.buffer];

                    const uint8_t* uvData = uvBuffer.data.data() + uvBufferView.byteOffset + uvAccessor.byteOffset;

                    for (size_t i = 0; i < uvAccessor.count; ++i) {
                        float u = 0.0f, v = 0.0f;

                        switch (uvAccessor.componentType) {
                            case TINYGLTF_COMPONENT_TYPE_BYTE:
                            {
                                const int8_t* uv = reinterpret_cast<const int8_t*>(uvData) + i * 2;
                                u = std::max(static_cast<float>(uv[0]) / 127.0f, -1.0f);
                                v = std::max(static_cast<float>(uv[1]) / 127.0f, -1.0f);
                                break;
                            }
                            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            {
                                u = static_cast<float>(uvData[i * 2]) / 255.0f;
                                v = static_cast<float>(uvData[i * 2 + 1]) / 255.0f;
                                break;
                            }
                            case TINYGLTF_COMPONENT_TYPE_SHORT:
                            {
                                const int16_t* uv = reinterpret_cast<const int16_t*>(uvData) + i * 2;
                                u = std::max(static_cast<float>(uv[0]) / 32767.0f, -1.0f);
                                v = std::max(static_cast<float>(uv[1]) / 32767.0f, -1.0f);
                                break;
                            }
                            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            {
                                const uint16_t* uv = reinterpret_cast<const uint16_t*>(uvData) + i * 2;
                                u = static_cast<float>(uv[0]) / 65535.0f;
                                v = static_cast<float>(uv[1]) / 65535.0f;
                                break;
                            }
                            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                            {
                                const float* uv = reinterpret_cast<const float*>(uvData) + i * 2;
                                u = uv[0];
                                v = uv[1];
                                break;
                            }
                            default:
                                SPDLOG_WARN("Unsupported UV component type: {}", uvAccessor.componentType);
                                break;
                        }

                        loadJob->rawData.vertices[vertexStart + i].texcoordU = u;
                        loadJob->rawData.vertices[vertexStart + i].texcoordV = v;
                    }
                }

                // COLOR_0 (Vertex Color)
                auto colorIt = primitive.attributes.find("COLOR_0");
                if (colorIt != primitive.attributes.end()) {
                    const auto& colorAccessor = model.accessors[colorIt->second];
                    const auto& colorBufferView = model.bufferViews[colorAccessor.bufferView];
                    const auto& colorBuffer = model.buffers[colorBufferView.buffer];

                    const float* colorData = reinterpret_cast<const float*>(
                        colorBuffer.data.data() + colorBufferView.byteOffset + colorAccessor.byteOffset
                    );

                    for (size_t i = 0; i < colorAccessor.count; ++i) {
                        if (colorAccessor.type == TINYGLTF_TYPE_VEC4) {
                            loadJob->rawData.vertices[vertexStart + i].color = glm::vec4(
                                colorData[i * 4], colorData[i * 4 + 1], colorData[i * 4 + 2], colorData[i * 4 + 3]
                            );
                        }
                        else if (colorAccessor.type == TINYGLTF_TYPE_VEC3) {
                            loadJob->rawData.vertices[vertexStart + i].color = glm::vec4(
                                colorData[i * 3], colorData[i * 3 + 1], colorData[i * 3 + 2], 1.0f
                            );
                        }
                    }
                }

                primData.boundingSphere = GenerateBoundingSphere(std::span(loadJob->rawData.vertices.data() + vertexStart, loadJob->rawData.vertices.size() - vertexStart));

                meshInfo.primitiveProperties.emplace_back(loadJob->rawData.primitives.size(), materialIndex);
                loadJob->rawData.primitives.push_back(primData);
            }

            loadJob->rawData.allMeshes.push_back(meshInfo);
        }
    }


    //
    {
        ZoneScopedN("ParseNodes");
        loadJob->rawData.nodes.reserve(model.nodes.size());
        for (const auto& gltfNode : model.nodes) {
            Render::Node node;
            node.name = gltfNode.name;
            node.meshIndex = gltfNode.mesh;
            node.parent = ~0u;

            if (!gltfNode.matrix.empty()) {
                // Matrix decomposition
                glm::mat4 mat;
                for (int i = 0; i < 16; ++i) {
                    mat[i / 4][i % 4] = gltfNode.matrix[i];
                }
                node.localTranslation = glm::vec3(mat[3]);
                node.localRotation = glm::quat_cast(mat);
                node.localScale = glm::vec3(
                    glm::length(glm::vec3(mat[0])),
                    glm::length(glm::vec3(mat[1])),
                    glm::length(glm::vec3(mat[2]))
                );
            }
            else {
                // TRS
                if (gltfNode.translation.size() == 3) {
                    node.localTranslation = glm::vec3(gltfNode.translation[0], gltfNode.translation[1], gltfNode.translation[2]);
                }
                if (gltfNode.rotation.size() == 4) {
                    node.localRotation = glm::quat(gltfNode.rotation[3], gltfNode.rotation[0], gltfNode.rotation[1], gltfNode.rotation[2]);
                }
                if (gltfNode.scale.size() == 3) {
                    node.localScale = glm::vec3(gltfNode.scale[0], gltfNode.scale[1], gltfNode.scale[2]);
                }
            }

            loadJob->rawData.nodes.push_back(node);
        }

        // Set parent indices
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            for (int childIdx : model.nodes[i].children) {
                loadJob->rawData.nodes[childIdx].parent = i;
            }
        }
    }

    //
    {
        ZoneScopedN("ParseSkins");
        if (!model.skins.empty()) {
            const auto& skin = model.skins[0];

            if (skin.inverseBindMatrices >= 0) {
                const auto& accessor = model.accessors[skin.inverseBindMatrices];
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];

                const float* data = reinterpret_cast<const float*>(
                    buffer.data.data() + bufferView.byteOffset + accessor.byteOffset
                );

                loadJob->rawData.inverseBindMatrices.resize(accessor.count);
                for (size_t i = 0; i < accessor.count; ++i) {
                    glm::mat4 mat;
                    for (int j = 0; j < 16; ++j) {
                        mat[j / 4][j % 4] = data[i * 16 + j];
                    }
                    loadJob->rawData.inverseBindMatrices[i] = mat;
                }

                // Set inverse bind indices on nodes
                for (size_t i = 0; i < skin.joints.size(); ++i) {
                    loadJob->rawData.nodes[skin.joints[i]].inverseBindIndex = i;
                }
            }
        }
    }

    //
    {
        ZoneScopedN("ParseAnimations");
        loadJob->rawData.animations.reserve(model.animations.size());
        for (const auto& gltfAnim : model.animations) {
            Render::Animation anim;
            anim.name = gltfAnim.name;

            // Samplers
            anim.samplers.reserve(gltfAnim.samplers.size());
            for (const auto& gltfSampler : gltfAnim.samplers) {
                Render::AnimationSampler sampler;

                // Input (timestamps)
                const auto& inputAccessor = model.accessors[gltfSampler.input];
                const auto& inputBufferView = model.bufferViews[inputAccessor.bufferView];
                const auto& inputBuffer = model.buffers[inputBufferView.buffer];
                const float* inputData = reinterpret_cast<const float*>(
                    inputBuffer.data.data() + inputBufferView.byteOffset + inputAccessor.byteOffset
                );

                sampler.timestamps.resize(inputAccessor.count);
                std::memcpy(sampler.timestamps.data(), inputData, inputAccessor.count * sizeof(float));

                // Output (values)
                const auto& outputAccessor = model.accessors[gltfSampler.output];
                const auto& outputBufferView = model.bufferViews[outputAccessor.bufferView];
                const auto& outputBuffer = model.buffers[outputBufferView.buffer];
                const float* outputData = reinterpret_cast<const float*>(
                    outputBuffer.data.data() + outputBufferView.byteOffset + outputAccessor.byteOffset
                );

                size_t componentCount = 1;
                if (outputAccessor.type == TINYGLTF_TYPE_VEC3) componentCount = 3;
                else if (outputAccessor.type == TINYGLTF_TYPE_VEC4) componentCount = 4;

                sampler.values.resize(outputAccessor.count * componentCount);
                std::memcpy(sampler.values.data(), outputData, sampler.values.size() * sizeof(float));

                // Interpolation
                if (gltfSampler.interpolation == "LINEAR") {
                    sampler.interpolation = Render::AnimationSampler::Interpolation::Linear;
                }
                else if (gltfSampler.interpolation == "STEP") {
                    sampler.interpolation = Render::AnimationSampler::Interpolation::Step;
                }
                else if (gltfSampler.interpolation == "CUBICSPLINE") {
                    sampler.interpolation = Render::AnimationSampler::Interpolation::CubicSpline;
                }

                anim.samplers.push_back(sampler);
            }

            // Channels
            anim.channels.reserve(gltfAnim.channels.size());
            for (const auto& gltfChannel : gltfAnim.channels) {
                Render::AnimationChannel channel;
                channel.samplerIndex = gltfChannel.sampler;
                channel.targetNodeIndex = gltfChannel.target_node;

                if (gltfChannel.target_path == "translation") {
                    channel.targetPath = Render::AnimationChannel::TargetPath::Translation;
                }
                else if (gltfChannel.target_path == "rotation") {
                    channel.targetPath = Render::AnimationChannel::TargetPath::Rotation;
                }
                else if (gltfChannel.target_path == "scale") {
                    channel.targetPath = Render::AnimationChannel::TargetPath::Scale;
                }
                else if (gltfChannel.target_path == "weights") {
                    channel.targetPath = Render::AnimationChannel::TargetPath::Weights;
                }

                anim.channels.push_back(channel);
            }

            // Calculate duration
            anim.duration = 0.0f;
            for (const auto& sampler : anim.samplers) {
                if (!sampler.timestamps.empty()) {
                    anim.duration = std::max(anim.duration, sampler.timestamps.back());
                }
            }

            loadJob->rawData.animations.push_back(anim);
        }
    }

    //
    {
        ZoneScopedN("CreateSamplers");
        loadJob->outputModel->modelData.samplers.reserve(model.samplers.size());
        for (const auto& gltfSampler : model.samplers) {
            VkSamplerCreateInfo samplerInfo = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
            samplerInfo.minLod = 0;

            // Mag filter
            samplerInfo.magFilter = (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
                                        ? VK_FILTER_NEAREST
                                        : VK_FILTER_LINEAR;

            // Min filter
            samplerInfo.minFilter = (gltfSampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
                                     gltfSampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                                     gltfSampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR)
                                        ? VK_FILTER_NEAREST
                                        : VK_FILTER_LINEAR;

            // Mipmap mode
            samplerInfo.mipmapMode = (gltfSampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                                      gltfSampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST)
                                         ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                         : VK_SAMPLER_MIPMAP_MODE_LINEAR;

            // Wrap modes
            auto convertWrap = [](int wrap) {
                switch (wrap) {
                    case TINYGLTF_TEXTURE_WRAP_REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                }
            };

            samplerInfo.addressModeU = convertWrap(gltfSampler.wrapS);
            samplerInfo.addressModeV = convertWrap(gltfSampler.wrapT);
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

            loadJob->outputModel->modelData.samplers.push_back(Render::Sampler::CreateSampler(loadJob->context, samplerInfo));
        }
    }

    //
    {
        ZoneScopedN("LoadTextures");
        // WIP - always append nullptr
        for (size_t i = 0; i < model.images.size(); ++i) {
            loadJob->pendingTextures.push_back(nullptr);
        }
    }

    loadJob->rawData.name = model.scenes.empty() ? "Loaded Model" : model.scenes[0].name;
    loadJob->rawData.bIsSkeletalModel = !model.skins.empty();
    loadJob->taskState = TaskState::Complete;
}
} // AssetLoad
