//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "asset_load_job.h"
#include "will_model_load_job.h"
#include "platform/paths.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
AssetLoadThread::AssetLoadThread() = default;

AssetLoadThread::~AssetLoadThread()
{
    if (context) {
        vkDestroyCommandPool(context->device, commandPool, nullptr);
    }
}

void AssetLoadThread::Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));
    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(WILL_MODEL_JOB_COUNT, commandPool);
    std::array<VkCommandBuffer, WILL_MODEL_JOB_COUNT + 1> commandBuffers{};
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, commandBuffers.data()));

    willModelJobs.reserve(WILL_MODEL_JOB_COUNT);
    for (int32_t i = 0; i < WILL_MODEL_JOB_COUNT; ++i) {
        auto willModelLoadJob = std::make_unique<WillModelLoadJob>(context, resourceManager, commandBuffers[i]);
        willModelJobs.emplace_back(std::move(willModelLoadJob));
    }

    CreateDefaultResources();
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
                job->SetOutputModel(loadRequest.willModelHandle, loadRequest.model);
                willModelJobActive[freeJobIdx] = true;

                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::WillModel;
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

                        // Find job index for deactivation
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
                        // TODO: Add texture job check here when implemented

                        // auto* texJob = static_cast<TextureLoadJob*>(job);
                        // textureCompleteLoadQueue.push({texJob->handle, texJob->target, success});
                        // job->Reset();
                        // textureJobActive[i] = false;

                        break;
                    }
                    default:
                        break;
                }

                // Free slot
                activeSlotMask[slotIdx] = false;
                slot.job = nullptr;
                slot.loadState = AssetLoadState::Unassigned;
                slot.type = AssetType::None;
            }
        }
    }
}

void AssetLoadThread::CreateDefaultResources()
{
    /*const auto testTexturePath = Platform::GetAssetPath() / "textures/error.ktx2";
    const uint32_t mipLevels = 1;

    ktxTexture2* texture;
    ktx_error_code_e result = ktxTexture2_CreateFromNamedFile(testTexturePath.string().c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &texture);
    assert(result == KTX_SUCCESS && "Failed to load test texture");

    auto& uploadStaging = assetLoadSlots[0].uploadStaging;

    ktx_size_t dataSize = texture->dataSize;
    OffsetAllocator::Allocation allocation = uploadStaging->GetStagingAllocator().allocate(dataSize);
    assert(allocation.metadata != OffsetAllocator::Allocation::NO_SPACE);

    VkExtent3D extent;
    extent.width = texture->baseWidth;
    extent.height = texture->baseHeight;
    extent.depth = texture->baseDepth;

    if (ktxTexture2_NeedsTranscoding(texture)) {
        // const ktx_transcode_fmt_e targetFormat = KTX_TTF_BC7_RGBA;
        const ktx_transcode_fmt_e targetFormat = KTX_TTF_RGBA32;
        result = ktxTexture2_TranscodeBasis(texture, targetFormat, 0);
        if (result != KTX_SUCCESS) {
            SPDLOG_ERROR("Failed to transcode texture");
            ktxTexture2_Destroy(texture);
            return;
        }
    }

    VkFormat imageFormat = ktxTexture2_GetVkFormat(texture);
    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(imageFormat, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.arrayLayers = texture->numLayers;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Render::AllocatedImage allocatedImage = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(allocatedImage.handle, allocatedImage.format, VK_IMAGE_ASPECT_COLOR_BIT);
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.layerCount = texture->numLayers;
    viewInfo.subresourceRange.levelCount = mipLevels;
    Render::ImageView imageView = Render::ImageView::CreateImageView(context, viewInfo);

    uploadStaging->StartCommandBuffer();
    char* bufferOffset = static_cast<char*>(uploadStaging->GetStagingBuffer().allocationInfo.pMappedData) + allocation.offset;
    memcpy(bufferOffset, texture->pData, dataSize);

    size_t mipOffset;
    ktxTexture_GetImageOffset(ktxTexture(texture), 0, 0, 0, &mipOffset);

    VkBufferImageCopy region{};
    region.bufferOffset = allocation.offset + mipOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = texture->numLayers;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {texture->baseWidth, texture->baseHeight, texture->baseDepth};

    VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
        allocatedImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, texture->numLayers),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

    vkCmdCopyBufferToImage(uploadStaging->GetCommandBuffer(), uploadStaging->GetStagingBuffer().handle, allocatedImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        allocatedImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, texture->numLayers),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    barrier.srcQueueFamilyIndex = context->transferQueueFamily;
    barrier.dstQueueFamilyIndex = context->graphicsQueueFamily;
    vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

    // model->imageAcquireOps.push_back(Render::VkHelpers::FromVkBarrier(barrier));
    // model->modelData.images.push_back(std::move(allocatedImage));
    // model->modelData.imageViews.push_back(std::move(imageView));

    uploadStaging->SubmitCommandBuffer();
    uploadStaging->WaitForFence();

    ktxTexture2_Destroy(texture);

    auto index = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateTexture({
        .imageView = imageView.handle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });
    assert(index.index == 0);*/
}
} // AssetLoad
