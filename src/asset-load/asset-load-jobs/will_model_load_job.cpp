//
// Created by William on 2025-12-23.
//

#include "will_model_load_job.h"

#include "asset-load/asset_load_config.h"
#include "ktxvulkan.h"
#include "render/model/model_serialization.h"
#include "render/model/will_model_asset.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
WillModelLoadSlot::WillModelLoadSlot()
{
    task = std::make_unique<LoadModelTask>();
    task->loadSlot = this;
}

WillModelLoadSlot::~WillModelLoadSlot()
{}

void WillModelLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    std::function<void(VkCommandBuffer cmd, VkFence fence)> dispatchCallback,
    std::function<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    _dispatchCommandBufferCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);
}

void WillModelLoadSlot::Launch(
    ModelSlotHandle _modelSlotHandle,
    UploadStagingSlotHandle _uploadStagingSlotHandle,
    UploadStaging* _uploadStaging,
    Render::WillModel* _outputModel)
{
    modelSlotHandle = _modelSlotHandle;
    uploadStagingSlotHandle = _uploadStagingSlotHandle;
    uploadStaging = _uploadStaging;
    outputModel = _outputModel;

    scheduler->AddTaskSetToPipe(task.get());
}

void WillModelLoadSlot::Clear()
{
    modelSlotHandle = {};
    uploadStagingSlotHandle = {};
    outputModel = nullptr;
    uploadStaging = nullptr;

    rawData.Reset();
    pendingTextures.clear();
    convertedVertices.clear();
    packedTriangles.clear();
}

void WillModelLoadSlot::LoadModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadModelFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->modelSlotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->modelSlotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    // Cannot fail past those first 2 checks
    loadSlot->PrepareUploadData();

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(loadSlot->context->transferQueueFamily);
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(loadSlot->context->device, &poolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, commandPool);
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(loadSlot->context->device, &cmdInfo, &cmd));

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(loadSlot->context->device, &fenceInfo, nullptr, &fence));

    auto submitAndWait = [&](bool reset) {
        ZoneScopedN("SubmitAndWait");

        VK_CHECK(vkEndCommandBuffer(cmd));
        loadSlot->_dispatchCommandBufferCallback(cmd, fence);

        VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

        if (reset) {
            VK_CHECK(vkResetFences(loadSlot->context->device, 1, &fence));
            VK_CHECK(vkResetCommandBuffer(cmd, 0));

            VkCommandBufferBeginInfo beginInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
        }
    };

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    loadSlot->UploadTextures(cmd, submitAndWait);
    loadSlot->UploadGeometry(cmd, submitAndWait);

    VK_CHECK(vkEndCommandBuffer(cmd));
    loadSlot->_dispatchCommandBufferCallback(cmd, fence);
    VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

    loadSlot->PostUploadSetup();

    vkDestroyFence(loadSlot->context->device, fence, nullptr);
    vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);

    loadSlot->_notifyCallback(true, loadSlot->modelSlotHandle, loadSlot->uploadStagingSlotHandle);
}

bool WillModelLoadSlot::LoadModelFromDisk()
{
    ZoneScopedN("LoadModelTask");
    //
    {
        ZoneScopedN("FileExistsCheck");
        if (!std::filesystem::exists(outputModel->source)) {
            SPDLOG_ERROR("Failed to find path to willmodel - {}", outputModel->name);
            return false;
        }
    }

    Render::ModelReader reader = Render::ModelReader(outputModel->source);

    if (!reader.GetSuccessfullyLoaded()) {
        SPDLOG_ERROR("Failed to load willmodel - {}", outputModel->name);
        return false;
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
        rawData.bIsSkeletalModel = header->bIsSkeletalModel;
        readArray(rawData.vertices, header->vertexCount);
        readArray(rawData.meshletVertices, header->meshletVertexCount);
        readArray(rawData.meshletTriangles, header->meshletTriangleCount);
        readArray(rawData.meshlets, header->meshletCount);
        readArray(rawData.primitives, header->primitiveCount);
        readArray(rawData.materials, header->materialCount);
    }

    dataPtr = modelBinData.data() + offset; {
        ZoneScopedN("ParseMeshes");
        rawData.allMeshes.resize(header->meshCount);
        for (uint32_t i = 0; i < header->meshCount; i++) {
            Render::ReadMeshInformation(dataPtr, rawData.allMeshes[i]);
        }
    } {
        ZoneScopedN("ParseNodes");
        rawData.nodes.resize(header->nodeCount);
        for (uint32_t i = 0; i < header->nodeCount; i++) {
            Render::ReadNode(dataPtr, rawData.nodes[i]);
        }
    } {
        ZoneScopedN("ParseAnimations");
        rawData.animations.resize(header->animationCount);
        for (uint32_t i = 0; i < header->animationCount; i++) {
            Render::ReadAnimation(dataPtr, rawData.animations[i]);
        }
    }

    offset = dataPtr - modelBinData.data(); {
        ZoneScopedN("ParseSkeletalData");
        readArray(rawData.inverseBindMatrices, header->inverseBindMatrixCount);
    }

    //
    {
        ZoneScopedN("CreateSamplers");
        std::vector<VkSamplerCreateInfo> samplerInfos{};
        readArray(samplerInfos, header->samplerCount);
        for (VkSamplerCreateInfo& sampler : samplerInfos) {
            outputModel->modelData.samplers.push_back(Render::Sampler::CreateSampler(context, sampler));
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
                pendingTextures.push_back(nullptr);
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
                        return false;
                    }
                }
            }

            assert(!ktxTexture2_NeedsTranscoding(loadedTexture) && "This engine no longer supports UASTC/ETC1S compressed textures");

            // Size check
            ktx_size_t mip0Size = ktxTexture_GetImageSize(ktxTexture(loadedTexture), 0);
            if (mip0Size > WILL_MODEL_LOAD_STAGING_SIZE) {
                SPDLOG_WARN("Texture too big to fit in the staging buffer for texture {}, pruning", textureName);
                pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            // Texture dimension and array check
            if (loadedTexture->numDimensions != 2) {
                SPDLOG_WARN("Engine does not support non 2D image textures {}, pruning", textureName);
                pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            if (loadedTexture->isArray) {
                SPDLOG_WARN("Engine does not support texture arrays {}, pruning", textureName);
                pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            if (loadedTexture->isCubemap) {
                SPDLOG_WARN("Texture does not support cubemaps {}, pruning", textureName);
                pendingTextures.push_back(nullptr);
                ktxTexture2_Destroy(loadedTexture);
                continue;
            }

            pendingTextures.push_back(loadedTexture);
        }
    }

    return true;
}

bool WillModelLoadSlot::AllocateGPUResources() const
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

    outputModel->modelData.bIsSkinned = rawData.bIsSkeletalModel;

    // Thread-safe allocation
    {
        std::lock_guard lock(rawData.bIsSkeletalModel ? resourceManager->skinnedVertexBufferAllocatorMutex : resourceManager->vertexBufferAllocatorMutex);
        outputModel->modelData.vertexAllocation = selectedAllocator->allocate(sizeVertices);
        if (outputModel->modelData.vertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            SPDLOG_ERROR("[WillModelLoadSlot] Not enough space in mega vertex buffer");
            return false;
        }
    }

    size_t sizeMeshletVertices = rawData.meshletVertices.size() * sizeof(uint32_t); {
        std::lock_guard lock(resourceManager->meshletVertexBufferAllocatorMutex);
        outputModel->modelData.meshletVertexAllocation = resourceManager->meshletVertexBufferAllocator.allocate(sizeMeshletVertices);
        if (outputModel->modelData.meshletVertexAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            std::lock_guard cleanupLock(rawData.bIsSkeletalModel ? resourceManager->skinnedVertexBufferAllocatorMutex : resourceManager->vertexBufferAllocatorMutex);
            selectedAllocator->free(outputModel->modelData.vertexAllocation);
            SPDLOG_ERROR("[WillModelLoadSlot] Not enough space in mega meshlet vertex buffer");
            return false;
        }
    }

    size_t sizeMeshletTriangles = rawData.meshletTriangles.size() / 3 * sizeof(uint32_t); {
        std::lock_guard lock(resourceManager->meshletTriangleBufferAllocatorMutex);
        outputModel->modelData.meshletTriangleAllocation = resourceManager->meshletTriangleBufferAllocator.allocate(sizeMeshletTriangles);
        if (outputModel->modelData.meshletTriangleAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            // Cleanup previous allocations
            {
                std::lock_guard cleanupLock(rawData.bIsSkeletalModel ? resourceManager->skinnedVertexBufferAllocatorMutex : resourceManager->vertexBufferAllocatorMutex);
                selectedAllocator->free(outputModel->modelData.vertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletVertexBufferAllocatorMutex);
                resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
            }
            SPDLOG_ERROR("[WillModelLoadSlot] Not enough space in mega meshlet triangle buffer");
            return false;
        }
    }

    size_t sizeMeshlets = rawData.meshlets.size() * sizeof(Meshlet); {
        std::lock_guard lock(resourceManager->meshletBufferAllocatorMutex);
        outputModel->modelData.meshletAllocation = resourceManager->meshletBufferAllocator.allocate(sizeMeshlets);
        if (outputModel->modelData.meshletAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            // Cleanup all previous allocations
            {
                std::lock_guard cleanupLock(rawData.bIsSkeletalModel ? resourceManager->skinnedVertexBufferAllocatorMutex : resourceManager->vertexBufferAllocatorMutex);
                selectedAllocator->free(outputModel->modelData.vertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletVertexBufferAllocatorMutex);
                resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletTriangleBufferAllocatorMutex);
                resourceManager->meshletTriangleBufferAllocator.free(outputModel->modelData.meshletTriangleAllocation);
            }
            SPDLOG_ERROR("[WillModelLoadSlot] Not enough space in mega meshlet buffer");
            return false;
        }
    }

    size_t sizePrimitives = rawData.primitives.size() * sizeof(MeshletPrimitive); {
        std::lock_guard lock(resourceManager->primitiveBufferAllocatorMutex);
        outputModel->modelData.primitiveAllocation = resourceManager->primitiveBufferAllocator.allocate(sizePrimitives);
        if (outputModel->modelData.primitiveAllocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            // Cleanup all previous allocations
            {
                std::lock_guard cleanupLock(rawData.bIsSkeletalModel ? resourceManager->skinnedVertexBufferAllocatorMutex : resourceManager->vertexBufferAllocatorMutex);
                selectedAllocator->free(outputModel->modelData.vertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletVertexBufferAllocatorMutex);
                resourceManager->meshletVertexBufferAllocator.free(outputModel->modelData.meshletVertexAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletTriangleBufferAllocatorMutex);
                resourceManager->meshletTriangleBufferAllocator.free(outputModel->modelData.meshletTriangleAllocation);
            } {
                std::lock_guard cleanupLock(resourceManager->meshletBufferAllocatorMutex);
                resourceManager->meshletBufferAllocator.free(outputModel->modelData.meshletAllocation);
            }
            SPDLOG_ERROR("[WillModelLoadSlot] Not enough space in mega primitive buffer");
            return false;
        }
    }

    return true;
}

void WillModelLoadSlot::PrepareUploadData()
{
    // Adjust offsets
    uint32_t vertexOffset = outputModel->modelData.vertexAllocation.offset /
                            (rawData.bIsSkeletalModel ? sizeof(SkinnedVertex) : sizeof(Vertex));
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

    // Move data to outputModel
    outputModel->modelData.meshes = std::move(rawData.allMeshes);
    outputModel->modelData.nodes = std::move(rawData.nodes);
    outputModel->modelData.inverseBindMatrices = std::move(rawData.inverseBindMatrices);
    outputModel->modelData.animations = std::move(rawData.animations);
    outputModel->modelData.materials = std::move(rawData.materials);

    // Convert vertices if needed
    if (!rawData.bIsSkeletalModel) {
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
    }

    // Pack triangles
    packedTriangles.reserve(rawData.meshletTriangles.size() / 3);
    for (size_t i = 0; i < rawData.meshletTriangles.size(); i += 3) {
        uint32_t packed = rawData.meshletTriangles[i + 0] |
                          (rawData.meshletTriangles[i + 1] << 8) |
                          (rawData.meshletTriangles[i + 2] << 16);
        packedTriangles.push_back(packed);
    }

    // Create images for textures
    for (auto currentTexture : pendingTextures) {
        if (currentTexture == nullptr) {
            outputModel->modelData.images.emplace_back();
            outputModel->modelData.imageViews.emplace_back();
        }
        else {
            VkExtent3D extent{
                .width = currentTexture->baseWidth,
                .height = currentTexture->baseHeight,
                .depth = currentTexture->baseDepth
            };

            VkFormat imageFormat = ktxTexture2_GetVkFormat(currentTexture);
            VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
                imageFormat, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            imageCreateInfo.mipLevels = currentTexture->numLevels;
            imageCreateInfo.arrayLayers = currentTexture->numLayers;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Render::AllocatedImage allocatedImage = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

            VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
                allocatedImage.handle, allocatedImage.format, VK_IMAGE_ASPECT_COLOR_BIT);
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.subresourceRange.layerCount = currentTexture->numLayers;
            viewInfo.subresourceRange.levelCount = currentTexture->numLevels;
            Render::ImageView imageView = Render::ImageView::CreateImageView(context, viewInfo);

            outputModel->modelData.images.push_back(std::move(allocatedImage));
            outputModel->modelData.imageViews.push_back(std::move(imageView));
        }
    }
}

void WillModelLoadSlot::UploadTextures(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadTextures");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    for (size_t textureIdx = 0; textureIdx < pendingTextures.size(); textureIdx++) {
        ktxTexture2* currentTexture = pendingTextures[textureIdx];
        if (currentTexture == nullptr) {
            continue;
        }

        Render::AllocatedImage& image = outputModel->modelData.images[textureIdx];

        // Pre-copy barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
            image.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, currentTexture->numLevels, 0, currentTexture->numLayers),
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );

        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &preCopyBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        // Upload all mip levels
        for (uint32_t mipLevel = 0; mipLevel < currentTexture->numLevels; mipLevel++) {
            ZoneScopedN("Upload Mip");

            size_t mipOffset;
            ktxTexture_GetImageOffset(ktxTexture(currentTexture), mipLevel, 0, 0, &mipOffset);
            uint32_t mipWidth = std::max(1u, currentTexture->baseWidth >> mipLevel);
            uint32_t mipHeight = std::max(1u, currentTexture->baseHeight >> mipLevel);
            uint32_t mipDepth = std::max(1u, currentTexture->baseDepth >> mipLevel);
            size_t mipSize = ktxTexture_GetImageSize(ktxTexture(currentTexture), mipLevel);

            size_t allocation = stagingAllocator.Allocate(mipSize);
            if (allocation == SIZE_MAX) {
                submitAndWait(true);
                stagingAllocator.Reset();
                allocation = stagingAllocator.Allocate(mipSize);
                assert(allocation != SIZE_MAX && "Mip level too large for staging buffer");
            }

            char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
            memcpy(stagingPtr, currentTexture->pData + mipOffset, mipSize);

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = allocation;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = mipLevel;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = currentTexture->numLayers;
            copyRegion.imageOffset = {0, 0, 0};
            copyRegion.imageExtent = {mipWidth, mipHeight, mipDepth};

            vkCmdCopyBufferToImage(
                cmd,
                stagingBuffer.handle,
                image.handle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copyRegion
            );
        }

        // Final barrier: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL with queue family transfer
        VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
            image.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, currentTexture->numLevels, 0, currentTexture->numLayers),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        finalBarrier.srcQueueFamilyIndex = context->transferQueueFamily;
        finalBarrier.dstQueueFamilyIndex = context->graphicsQueueFamily;

        depInfo.pImageMemoryBarriers = &finalBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        outputModel->imageAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(finalBarrier));

        ktxTexture2_Destroy(currentTexture);
    }

    pendingTextures.clear();
}

void WillModelLoadSlot::UploadGeometry(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadGeometry");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    auto uploadBuffer = [&](const void* sourceData, size_t count, size_t elementSize,
                           VkBuffer targetBuffer, VkDeviceSize targetOffset) {
        size_t totalSize = count * elementSize;
        size_t uploaded = 0;

        while (uploaded < totalSize) {
            size_t remaining = totalSize - uploaded;
            size_t allocation = stagingAllocator.Allocate(remaining);

            if (allocation == SIZE_MAX) {
                size_t freeSpace = stagingAllocator.GetRemaining();
                if (freeSpace == 0) {
                    submitAndWait(true);
                    stagingAllocator.Reset();
                    continue;
                }
                remaining = std::min(remaining, freeSpace);
                allocation = stagingAllocator.Allocate(remaining);
                assert(allocation != SIZE_MAX);
            }

            const char* srcPtr = static_cast<const char*>(sourceData) + uploaded;
            char* dstPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
            memcpy(dstPtr, srcPtr, remaining);

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = allocation;
            copyRegion.dstOffset = targetOffset + uploaded;
            copyRegion.size = remaining;
            vkCmdCopyBuffer(cmd, stagingBuffer.handle, targetBuffer, 1, &copyRegion);

            uploaded += remaining;
        }
    };

    // Upload vertices
    size_t vertexSize = rawData.bIsSkeletalModel ? sizeof(SkinnedVertex) : sizeof(Vertex);
    VkBuffer targetVertexBuffer = rawData.bIsSkeletalModel ?
        resourceManager->megaSkinnedVertexBuffer.handle : resourceManager->megaVertexBuffer.handle;
    const void* vertexData = rawData.bIsSkeletalModel ?
        static_cast<const void*>(rawData.vertices.data()) : static_cast<const void*>(convertedVertices.data());

    uploadBuffer(vertexData, rawData.vertices.size(), vertexSize,
                targetVertexBuffer, outputModel->modelData.vertexAllocation.offset);

    uploadBuffer(rawData.meshletVertices.data(), rawData.meshletVertices.size(), sizeof(uint32_t),
                resourceManager->megaMeshletVerticesBuffer.handle, outputModel->modelData.meshletVertexAllocation.offset);

    uploadBuffer(packedTriangles.data(), packedTriangles.size(), sizeof(uint32_t),
                resourceManager->megaMeshletTrianglesBuffer.handle, outputModel->modelData.meshletTriangleAllocation.offset);

    uploadBuffer(rawData.meshlets.data(), rawData.meshlets.size(), sizeof(Meshlet),
                resourceManager->megaMeshletBuffer.handle, outputModel->modelData.meshletAllocation.offset);

    uploadBuffer(rawData.primitives.data(), rawData.primitives.size(), sizeof(MeshletPrimitive),
                resourceManager->primitiveBuffer.handle, outputModel->modelData.primitiveAllocation.offset);

    // Queue family transfer barriers
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

    releaseBarriers.push_back(createBufferBarrier(targetVertexBuffer,
        outputModel->modelData.vertexAllocation.offset, rawData.vertices.size() * vertexSize));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->megaMeshletVerticesBuffer.handle,
        outputModel->modelData.meshletVertexAllocation.offset, rawData.meshletVertices.size() * sizeof(uint32_t)));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->megaMeshletTrianglesBuffer.handle,
        outputModel->modelData.meshletTriangleAllocation.offset, rawData.meshletTriangles.size() * sizeof(uint32_t)));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->megaMeshletBuffer.handle,
        outputModel->modelData.meshletAllocation.offset, rawData.meshlets.size() * sizeof(Meshlet)));
    releaseBarriers.push_back(createBufferBarrier(resourceManager->primitiveBuffer.handle,
        outputModel->modelData.primitiveAllocation.offset, rawData.primitives.size() * sizeof(MeshletPrimitive)));

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.bufferMemoryBarrierCount = releaseBarriers.size();
    depInfo.pBufferMemoryBarriers = releaseBarriers.data();
    vkCmdPipelineBarrier2(cmd, &depInfo);

    for (auto& barrier : releaseBarriers) {
        outputModel->bufferAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));
    }
}

void WillModelLoadSlot::PostUploadSetup()
{
    // Remap samplers
    auto remapSamplers = [](auto& indices, const std::vector<Render::BindlessSamplerHandle>& map) {
        indices.x = indices.x >= 0 ? map[indices.x].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
        indices.y = indices.y >= 0 ? map[indices.y].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
        indices.z = indices.z >= 0 ? map[indices.z].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
        indices.w = indices.w >= 0 ? map[indices.w].index : DEFAULT_SAMPLER_BINDLESS_INDEX;
    };

    outputModel->modelData.samplerIndexToDescriptorBufferIndexMap.resize(outputModel->modelData.samplers.size());
    for (int32_t i = 0; i < outputModel->modelData.samplers.size(); ++i) {
        outputModel->modelData.samplerIndexToDescriptorBufferIndexMap[i] =
            resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateSampler(outputModel->modelData.samplers[i].handle);
    }

    for (MaterialProperties& material : outputModel->modelData.materials) {
        remapSamplers(material.textureSamplerIndices, outputModel->modelData.samplerIndexToDescriptorBufferIndexMap);
        remapSamplers(material.textureSamplerIndices2, outputModel->modelData.samplerIndexToDescriptorBufferIndexMap);
    }

    // Remap textures
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

        outputModel->modelData.textureIndexToDescriptorBufferIndexMap[i] =
            resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateTexture({
                .imageView = outputModel->modelData.imageViews[i].handle,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
    }

    for (MaterialProperties& material : outputModel->modelData.materials) {
        remapTextures(material.textureImageIndices, outputModel->modelData.textureIndexToDescriptorBufferIndexMap);
        remapTextures(material.textureImageIndices2, outputModel->modelData.textureIndexToDescriptorBufferIndexMap);
    }
}
} // AssetLoad
