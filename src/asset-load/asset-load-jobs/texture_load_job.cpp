//
// Created by William on 2025-12-23.
//

#include "texture_load_job.h"

#include "asset-load/asset_load_config.h"
#include "asset-load/asset_load_types.h"
#include "ktxvulkan.h"
#include "render/texture_asset.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
TextureLoadJob::TextureLoadJob() = default;

TextureLoadJob::TextureLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager, VkCommandBuffer commandBuffer)
    : context(context), resourceManager(resourceManager), commandBuffer(commandBuffer)
{
    task = std::make_unique<LoadTextureTask>();
}

TextureLoadJob::~TextureLoadJob() = default;

void TextureLoadJob::StartJob()
{
    if (!uploadStaging) {
        uploadStaging = std::make_unique<UploadStaging>(context, commandBuffer, TEXTURE_LOAD_STAGING_SIZE);
    }
}

TaskState TextureLoadJob::TaskExecute(enki::TaskScheduler* scheduler)
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

bool TextureLoadJob::PreThreadExecute()
{
    if (!outputTexture || !texture) {
        return false;
    }

    VkExtent3D extent{
        .width = texture->baseWidth,
        .height = texture->baseHeight,
        .depth = texture->baseDepth
    };

    VkFormat imageFormat = ktxTexture2_GetVkFormat(texture);
    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        imageFormat,
        extent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = texture->numLevels;
    imageCreateInfo.arrayLayers = texture->numLayers;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    outputTexture->image = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        outputTexture->image.handle,
        outputTexture->image.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.layerCount = texture->numLayers;
    viewInfo.subresourceRange.levelCount = texture->numLevels;

    outputTexture->imageView = Render::ImageView::CreateImageView(context, viewInfo);

    // Validate lowest mip level is smaller than staging size
    size_t mipSize = ktxTexture_GetImageSize(ktxTexture(texture), 0);
    if (mipSize >= TEXTURE_LOAD_STAGING_SIZE) {
        return false;
    }

    return true;
}

ThreadState TextureLoadJob::ThreadExecute()
{
    ZoneScopedN("TextureLoadJob::ThreadExecute");
    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    if (!uploadStaging->IsReady()) {
        return ThreadState::InProgress;
    }


    if (bPendingInitialBarrier) {
        uploadStaging->StartCommandBuffer();
        VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
            outputTexture->image.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->numLevels, 0, texture->numLayers),
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );

        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &preCopyBarrier;
        vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

        bPendingInitialBarrier = false;
    }


    for (; currentMip < texture->numLevels; currentMip++) {
        uploadStaging->StartCommandBuffer();
        ZoneScopedN("Upload Mip");
        size_t mipOffset;
        ktxTexture_GetImageOffset(ktxTexture(texture), currentMip, 0, 0, &mipOffset);
        uint32_t mipWidth = std::max(1u, texture->baseWidth >> currentMip);
        uint32_t mipHeight = std::max(1u, texture->baseHeight >> currentMip);
        uint32_t mipDepth = std::max(1u, texture->baseDepth >> currentMip);
        size_t mipSize = ktxTexture_GetImageSize(ktxTexture(texture), currentMip);

        size_t allocation = stagingAllocator.Allocate(mipSize);
        if (allocation == SIZE_MAX) {
            uploadStaging->SubmitCommandBuffer();
            uploadCount++;
            return ThreadState::InProgress;
        }

        char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
        memcpy(stagingPtr, texture->pData + mipOffset, mipSize);


        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = allocation;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = currentMip;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = texture->numLayers;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {mipWidth, mipHeight, mipDepth};

        vkCmdCopyBufferToImage(
            uploadStaging->GetCommandBuffer(),
            stagingBuffer.handle,
            outputTexture->image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion
        );
    }

    if (bPendingFinalBarrier) {
        uploadStaging->StartCommandBuffer();
        VkImageMemoryBarrier2 finalBarrier;
        finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
            outputTexture->image.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->numLevels, 0, texture->numLayers),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        finalBarrier.srcQueueFamilyIndex = context->transferQueueFamily;
        finalBarrier.dstQueueFamilyIndex = context->graphicsQueueFamily;

        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &finalBarrier;
        vkCmdPipelineBarrier2(uploadStaging->GetCommandBuffer(), &depInfo);

        outputTexture->acquireBarrier = Render::VkHelpers::FromVkBarrier(finalBarrier);
        bPendingFinalBarrier = false;
    }

    if (uploadStaging->IsCommandBufferStarted()) {
        uploadStaging->SubmitCommandBuffer();
        uploadCount++;
        return ThreadState::InProgress;
    }

    return ThreadState::Complete;
}

bool TextureLoadJob::PostThreadExecute()
{
    if (!outputTexture) {
        return false;
    }

    bool updateRes = resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateTexture(
        outputTexture->bindlessHandle, {
            .imageView = outputTexture->imageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });

    if (!updateRes) {
        return false;
    }

    return true;
}

uint32_t TextureLoadJob::GetUploadCount()
{
    return uploadCount;
}

void TextureLoadJob::Reset()
{
    if (texture) {
        ktxTexture2_Destroy(texture);
        texture = nullptr;
    }

    taskState = TaskState::NotStarted;
    textureHandle = Engine::TextureHandle::INVALID;
    outputTexture = nullptr;
    currentMip = 0;
    bPendingFinalBarrier = true;
    bPendingInitialBarrier = true;
    uploadCount = 0;
}

void TextureLoadJob::LoadTextureTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
{
    ZoneScopedN("LoadTextureTask::ExecuteRange");
    if (!loadJob) {
        return;
    }

    if (!loadJob->outputTexture) {
        loadJob->taskState = TaskState::Failed;
        return;
    }

    const std::filesystem::path& texturePath = loadJob->outputTexture->source;

    if (!std::filesystem::exists(texturePath)) {
        SPDLOG_ERROR("[TextureLoadJob] Failed to find texture: {}", texturePath.string());
        loadJob->taskState = TaskState::Failed;
        return;
    }

    ktxTexture2* _texture;
    //
    {
        ZoneScopedN("KTXCreateFromMemory");
        ktx_error_code_e result = ktxTexture2_CreateFromNamedFile(
            texturePath.string().c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &_texture
        );

        if (result != KTX_SUCCESS) {
            SPDLOG_ERROR("[TextureLoadJob] Failed to load KTX texture: {}", texturePath.string());
            loadJob->taskState = TaskState::Failed;
            return;
        }
    }


    assert(!ktxTexture2_NeedsTranscoding(_texture) && "This engine no longer supports UASTC/ETC1S compressed textures");

    // Validate size
    if (_texture->dataSize > TEXTURE_LOAD_STAGING_SIZE) {
        SPDLOG_ERROR("[TextureLoadJob] Texture too large for staging buffer: {}", texturePath.string());
        ktxTexture2_Destroy(_texture);
        loadJob->taskState = TaskState::Failed;
        return;
    }

    // Validate it's 2D, not array, not cubemap
    // todo: for raw textures, add support for cubemap (for IBL)
    if (_texture->numDimensions != 2 || _texture->isArray || _texture->isCubemap) {
        SPDLOG_ERROR("[TextureLoadJob] Only 2D textures supported: {}", texturePath.string());
        ktxTexture2_Destroy(_texture);
        loadJob->taskState = TaskState::Failed;
        return;
    }

    loadJob->texture = _texture;
    loadJob->taskState = TaskState::Complete;
}
} // AssetLoad
