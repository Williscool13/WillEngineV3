//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "asset_load_job.h"
#include "ktxvulkan.h"
#include "texture_load_job.h"
#include "will_model_load_job.h"
#include "platform/paths.h"
#include "render/texture_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
AssetLoadThread::AssetLoadThread() = default;

AssetLoadThread::AssetLoadThread(enki::TaskScheduler* scheduler, Render::VulkanContext* context, Render::ResourceManager* resourceManager)
    : context(context), resourceManager(resourceManager), scheduler(scheduler)
{
    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));

    const uint32_t totalCommandBuffers = WILL_MODEL_JOB_COUNT + TEXTURE_JOB_COUNT + 1;
    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(totalCommandBuffers, commandPool);
    std::vector<VkCommandBuffer> commandBuffers(totalCommandBuffers);
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, commandBuffers.data()));


    willModelJobs.reserve(WILL_MODEL_JOB_COUNT);
    for (int32_t i = 0; i < WILL_MODEL_JOB_COUNT; ++i) {
        willModelJobs.emplace_back(std::make_unique<WillModelLoadJob>(context, resourceManager, commandBuffers[i]));
    }

    textureJobs.reserve(TEXTURE_JOB_COUNT);
    for (int32_t i = 0; i < TEXTURE_JOB_COUNT; ++i) {
        textureJobs.emplace_back(std::make_unique<TextureLoadJob>(context, resourceManager, commandBuffers[WILL_MODEL_JOB_COUNT + i]));
    }

    synchronousTextureUploadStaging = std::make_unique<UploadStaging>(context, commandBuffers[WILL_MODEL_JOB_COUNT + TEXTURE_JOB_COUNT], TEXTURE_LOAD_STAGING_SIZE);

    auto errorPath = Platform::GetAssetPath() / "textures/error.ktx2";
    auto whitePath = Platform::GetAssetPath() / "textures/white.ktx2";
    defaultErrorTexture = SynchronousLoadTexture(errorPath);
    defaultWhiteTexture = SynchronousLoadTexture(whitePath);

    assert(defaultWhiteTexture.image.handle != VK_NULL_HANDLE);
    whiteHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateTexture({
        .imageView = defaultWhiteTexture.imageView.handle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });
    assert(whiteHandle.index == 0);

    assert(defaultErrorTexture.image.handle != VK_NULL_HANDLE);
    errorHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateTexture({
        .imageView = defaultErrorTexture.imageView.handle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });
    assert(errorHandle.index == 1);
}

AssetLoadThread::~AssetLoadThread()
{
    if (context) {
        vkDestroyCommandPool(context->device, commandPool, nullptr);
    }
}

void AssetLoadThread::Start()
{
    bShouldExit.store(false, std::memory_order_release);

    uint32_t assetLoadThreadNum = scheduler->GetNumTaskThreads() - 2;
    pinnedTask = std::make_unique<enki::LambdaPinnedTask>(
        assetLoadThreadNum,
        [this] { ThreadMain(); }
    );

    scheduler->AddPinnedTask(pinnedTask.get());
}

void AssetLoadThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
}

void AssetLoadThread::Join() const
{
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void AssetLoadThread::RequestLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr)
{
    modelLoadQueue.push({willmodelHandle, willModelPtr});
}

bool AssetLoadThread::ResolveLoads(WillModelComplete& modelComplete)
{
    return modelCompleteLoadQueue.pop(modelComplete);
}

void AssetLoadThread::RequestUnLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr)
{
    modelUnloadQueue.push({willmodelHandle, willModelPtr});
}

bool AssetLoadThread::ResolveUnload(WillModelComplete& modelComplete)
{
    return modelCompleteUnloadQueue.pop(modelComplete);
}

void AssetLoadThread::RequestTextureLoad(Engine::TextureHandle textureHandle, Render::Texture* texturePtr)
{
    textureLoadQueue.push({textureHandle, texturePtr});
}

bool AssetLoadThread::ResolveTextureLoads(TextureComplete& textureComplete)
{
    return textureCompleteLoadQueue.pop(textureComplete);
}

void AssetLoadThread::RequestTextureUnload(Engine::TextureHandle textureHandle, Render::Texture* texturePtr)
{
    textureUnloadQueue.push({textureHandle, texturePtr});
}

bool AssetLoadThread::ResolveTextureUnload(TextureComplete& textureComplete)
{
    return textureCompleteUnloadQueue.pop(textureComplete);
}

Render::Texture AssetLoadThread::SynchronousLoadTexture(std::filesystem::path source)
{
    Render::Texture outTexture;
    outTexture.source = source;
    outTexture.name = source.filename().string();
    outTexture.loadState = Render::Texture::LoadState::Loaded;
    ktxTexture2* ktxTex;
    ktx_error_code_e result = ktxTexture2_CreateFromNamedFile(
        source.string().c_str(),
        KTX_TEXTURE_CREATE_NO_FLAGS,
        &ktxTex
    );

    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[AssetLoadThread] Failed to load texture: {}", source.filename().string());
        return {};
    }

    if (ktxTexture2_NeedsTranscoding(ktxTex)) {
        // const ktx_transcode_fmt_e targetFormat = KTX_TTF_BC7_RGBA;
        const ktx_transcode_fmt_e targetFormat = KTX_TTF_RGBA32;
        result = ktxTexture2_TranscodeBasis(ktxTex, targetFormat, 0);
        if (result != KTX_SUCCESS) {
            SPDLOG_ERROR("[AssetLoadThread] Failed to transcode texture: {}", source.filename().string());
            ktxTexture2_Destroy(ktxTex);
            return {};
        }
    }

    VkExtent3D extent{
        .width = ktxTex->baseWidth,
        .height = ktxTex->baseHeight,
        .depth = ktxTex->baseDepth
    };

    VkFormat imageFormat = ktxTexture2_GetVkFormat(ktxTex);
    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        imageFormat,
        extent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = ktxTex->numLevels;
    imageCreateInfo.arrayLayers = ktxTex->numLayers;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    outTexture.image = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        outTexture.image.handle,
        outTexture.image.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.layerCount = ktxTex->numLayers;
    viewInfo.subresourceRange.levelCount = ktxTex->numLevels;

    outTexture.imageView = Render::ImageView::CreateImageView(context, viewInfo);

    OffsetAllocator::Allocator& stagingAllocator = synchronousTextureUploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = synchronousTextureUploadStaging->GetStagingBuffer();
    VkCommandBuffer cmd = synchronousTextureUploadStaging->GetCommandBuffer();

    synchronousTextureUploadStaging->StartCommandBuffer();

    for (uint32_t mip = 0; mip < ktxTex->numLevels; mip++) {
        size_t mipOffset;
        ktxTexture_GetImageOffset(ktxTexture(ktxTex), mip, 0, 0, &mipOffset);

        uint32_t mipWidth = std::max(1u, ktxTex->baseWidth >> mip);
        uint32_t mipHeight = std::max(1u, ktxTex->baseHeight >> mip);
        uint32_t mipDepth = std::max(1u, ktxTex->baseDepth >> mip);

        size_t mipSize = ktxTexture_GetImageSize(ktxTexture(ktxTex), mip);

        OffsetAllocator::Allocation allocation = stagingAllocator.allocate(mipSize);
        if (allocation.metadata == OffsetAllocator::Allocation::NO_SPACE) {
            SPDLOG_ERROR("[AssetLoadThread] Staging buffer too small for texture: {}", source.filename().string());
            ktxTexture2_Destroy(ktxTex);
            return {};
        }

        char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation.offset;
        memcpy(stagingPtr, ktxTex->pData + mipOffset, mipSize);

        VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
            outTexture.image.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, ktxTex->numLayers),
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &preCopyBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = allocation.offset;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = mip;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = ktxTex->numLayers;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {mipWidth, mipHeight, mipDepth};

        vkCmdCopyBufferToImage(
            cmd,
            stagingBuffer.handle,
            outTexture.image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion
        );
    }

    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outTexture.image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, ktxTex->numLevels, 0, ktxTex->numLayers),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    // todo: need acquire barrier
    VkDependencyInfo finalDepInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    finalDepInfo.imageMemoryBarrierCount = 1;
    finalDepInfo.pImageMemoryBarriers = &finalBarrier;
    vkCmdPipelineBarrier2(cmd, &finalDepInfo);

    // outputTexture->acquireBarrier = Render::VkHelpers::FromVkBarrier(finalBarrier);

    synchronousTextureUploadStaging->SubmitCommandBuffer();

    ktxTexture2_Destroy(ktxTex);
    return outTexture;
}

void AssetLoadThread::ThreadMain()
{
    while (!bShouldExit.load(std::memory_order_acquire)) {
        // Model loading jobs
        {
            // Count free model load jobs (4 max)
            size_t freeJobCount = 0;
            for (size_t i = 0; i < willModelJobActive.size(); ++i) {
                if (!willModelJobActive[i]) {
                    freeJobCount++;
                }
            }

            // Only pop as many requests as we have free jobs
            for (size_t jobsStarted = 0; jobsStarted < freeJobCount; ++jobsStarted) {
                WillModelLoadRequest loadRequest{};
                if (!modelLoadQueue.pop(loadRequest)) {
                    break;
                }


                int32_t slotIdx = -1;
                for (size_t i = 0; i < 64; ++i) {
                    if (!(activeSlotMask[i])) {
                        slotIdx = i;
                        break;
                    }
                }

                // Find free job (guaranteed to exist)
                int32_t freeJobIdx = -1;
                for (size_t i = 0; i < willModelJobActive.size(); ++i) {
                    if (!willModelJobActive[i]) {
                        freeJobIdx = i;
                        break;
                    }
                }

                WillModelLoadJob* job = willModelJobs[freeJobIdx].get();
                job->willModelHandle = loadRequest.willModelHandle;
                job->outputModel = loadRequest.model;
                willModelJobActive[freeJobIdx] = true;

                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::WillModel;
                activeSlotMask[slotIdx] = true;
            }
        }

        // Texture loading jobs
        {
            size_t freeTextureJobCount = 0;
            for (size_t i = 0; i < textureJobActive.size(); ++i) {
                if (!textureJobActive[i]) {
                    freeTextureJobCount++;
                }
            }

            for (size_t jobsStarted = 0; jobsStarted < freeTextureJobCount; ++jobsStarted) {
                TextureLoadRequest loadRequest{};
                if (!textureLoadQueue.pop(loadRequest)) {
                    break;
                }

                int32_t slotIdx = -1;
                for (size_t i = 0; i < 64; ++i) {
                    if (!(activeSlotMask[i])) {
                        slotIdx = i;
                        break;
                    }
                }

                int32_t freeJobIdx = -1;
                for (size_t i = 0; i < textureJobActive.size(); ++i) {
                    if (!textureJobActive[i]) {
                        freeJobIdx = i;
                        break;
                    }
                }

                TextureLoadJob* job = textureJobs[freeJobIdx].get();
                job->textureHandle = loadRequest.textureHandle;
                job->outputTexture = loadRequest.texture;
                textureJobActive[freeJobIdx] = true;

                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::Texture;
                activeSlotMask[slotIdx] = true;
            }
        }

        for (size_t slotIdx = 0; slotIdx < 64; ++slotIdx) {
            if (!activeSlotMask[slotIdx]) {
                continue;
            }

            AssetLoadSlot& slot = assetLoadSlots[slotIdx];
            AssetLoadJob* job = slot.job;

            switch (slot.loadState) {
                case AssetLoadState::Idle:
                {
                    job->TaskExecute(scheduler);
                    slot.loadState = AssetLoadState::TaskExecuting;
                }
                break;

                case AssetLoadState::TaskExecuting:
                {
                    TaskState res = job->TaskExecute(scheduler);
                    if (res == TaskState::Failed) {
                        slot.loadState = AssetLoadState::Failed;
                    }
                    else if (res == TaskState::Complete) {
                        bool preRes = job->PreThreadExecute();
                        if (preRes) {
                            slot.loadState = AssetLoadState::ThreadExecuting;
                        }
                        else {
                            slot.loadState = AssetLoadState::Failed;
                        }
                    }
                }
                break;

                case AssetLoadState::ThreadExecuting:
                {
                    ThreadState res = job->ThreadExecute();
                    if (res == ThreadState::Complete) {
                        bool postRes = job->PostThreadExecute();
                        if (postRes) {
                            slot.loadState = AssetLoadState::Loaded;
                        }
                        else {
                            slot.loadState = AssetLoadState::Failed;
                        }
                    }
                }
                break;

                default:
                    break;
            }

            if (slot.loadState == AssetLoadState::Loaded || slot.loadState == AssetLoadState::Failed) {
                bool success = slot.loadState == AssetLoadState::Loaded;

                switch (slot.type) {
                    case AssetType::WillModel:
                    {
                        auto* modelJob = static_cast<WillModelLoadJob*>(job);
                        modelCompleteLoadQueue.push({modelJob->willModelHandle, modelJob->outputModel, success});

                        for (size_t i = 0; i < willModelJobs.size(); ++i) {
                            if (willModelJobs[i].get() == job) {
                                job->Reset();
                                willModelJobActive[i] = false;
                                break;
                            }
                        }
                        break;
                    }
                    case AssetType::Texture:
                    {
                        auto* textureJob = static_cast<TextureLoadJob*>(job);
                        textureCompleteLoadQueue.push({textureJob->textureHandle, textureJob->outputTexture, success});

                        for (size_t i = 0; i < textureJobs.size(); ++i) {
                            if (textureJobs[i].get() == job) {
                                job->Reset();
                                textureJobActive[i] = false;
                                break;
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }

                activeSlotMask[slotIdx] = false;
                slot.job = nullptr;
                slot.loadState = AssetLoadState::Unassigned;
                slot.type = AssetType::None;
            }
        }


        WillModelLoadRequest unloadRequest{};
        if (modelUnloadQueue.pop(unloadRequest)) {
            OffsetAllocator::Allocator* selectedAllocator;
            if (unloadRequest.model->modelData.bIsSkinned) {
                selectedAllocator = &resourceManager->skinnedVertexBufferAllocator;
            }
            else {
                selectedAllocator = &resourceManager->vertexBufferAllocator;
            }

            selectedAllocator->free(unloadRequest.model->modelData.vertexAllocation);
            resourceManager->meshletVertexBufferAllocator.free(unloadRequest.model->modelData.meshletVertexAllocation);
            resourceManager->meshletTriangleBufferAllocator.free(unloadRequest.model->modelData.meshletTriangleAllocation);
            resourceManager->meshletBufferAllocator.free(unloadRequest.model->modelData.meshletAllocation);
            resourceManager->primitiveBufferAllocator.free(unloadRequest.model->modelData.primitiveAllocation);
            unloadRequest.model->modelData.vertexAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletVertexAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletTriangleAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.primitiveAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;

            modelCompleteUnloadQueue.push({unloadRequest.willModelHandle, unloadRequest.model, true});
        }

        TextureLoadRequest textureUnloadRequest{};
        if (textureUnloadQueue.pop(textureUnloadRequest)) {
            if (textureUnloadRequest.texture->bindlessHandle.index != 0) {
                resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(textureUnloadRequest.texture->bindlessHandle);
            }

            textureUnloadRequest.texture->image = {};
            textureUnloadRequest.texture->imageView = {};

            textureCompleteUnloadQueue.push({textureUnloadRequest.textureHandle, textureUnloadRequest.texture, true});
        }
    }
}
} // AssetLoad
