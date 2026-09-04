//
// Created by William on 2026-02-03.
//

#include "texture_generate_slot.h"

#include "asset_generator.h"

#include "engine/logging/engine_log.h"
#include <stb/stb_image.h>
#include <tracy/Tracy.hpp>

#include "asset_generation_types.h"
#include "bc7enc_rdo/rdo_bc_encoder.h"
#include "core/memory/memory_manager.h"
#include "engine/compression/compression.h"
#include "engine/resources/texture/texture_format.h"
#include "engine/resources/wimage_format.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/shaders/constants_interop.h"
#include "render/shaders/push_constant_interop.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace Editor
{
TextureGenerateSlot::TextureGenerateSlot() = default;

TextureGenerateSlot::~TextureGenerateSlot()
{
    computeSubmit.Destroy(context);
}

void TextureGenerateSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::PipelineManager* _pipelineManager,
    Core::MemoryManager* _memoryManager,
    AssetGenerator* _assetGenerator,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, TextureGenerateSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    pipelineManager = _pipelineManager;
    memoryManager = _memoryManager;
    assetGenerator = _assetGenerator;
    _dispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    imageStagingBuffer = Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, TEXTURE_GENERATION_STAGING_BUFFER_SIZE);
    imageReceivingBuffer = Render::AllocatedBuffer::CreateAllocatedReceivingBuffer(context, TEXTURE_GENERATION_STAGING_BUFFER_SIZE);

    downsampleResources = Render::ProceduralTextureGenerateResources(context);

    const uint32_t submitFamily = context->computeQueue != VK_NULL_HANDLE ? context->computeQueueFamily : context->graphicsQueueFamily;
    computeSubmit.Initialize(context, submitFamily);
}

void TextureGenerateSlot::Launch(TextureGenerateSlotHandle _slotHandle, const Core::Path& _imagePath, const Core::Path& _outputPath, Engine::TextureID _textureId,
                                 bool _mipmapped, DXGI_FORMAT _targetFormat, uint64_t _contentVersion, bool _flipY,
                                 const Core::InlineString<128>& _declaredName, const Core::InlineString<256>& _recipeSource, Engine::TextureCategory _category,
                                 uint64_t _ownerModelId, uint32_t _ownerImageIndex)
{
    slotHandle = _slotHandle;
    imagePath = _imagePath;
    outputPath = _outputPath;
    textureId = _textureId;
    contentVersion = _contentVersion;
    mipmapped = _mipmapped;
    flipY = _flipY;
    targetFormat = _targetFormat;
    declaredName = _declaredName;
    recipeSource = _recipeSource;
    category = _category;
    ownerModelId = _ownerModelId;
    ownerImageIndex = _ownerImageIndex;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }

    task.taskSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void TextureGenerateSlot::LaunchFromMemory(TextureGenerateSlotHandle _slotHandle, Core::HeapArray<uint8_t> pixels, uint32_t w, uint32_t h, uint32_t bytesPerPixel,
                                           const Core::Path& _outputPath, Engine::TextureID _textureId, bool _mipmapped, DXGI_FORMAT _targetFormat, uint64_t _contentVersion, Engine::TextureCategory _category,
                                           uint64_t _ownerModelId, uint32_t _ownerImageIndex)
{
    slotHandle = _slotHandle;
    outputPath = _outputPath;
    textureId = _textureId;
    contentVersion = _contentVersion;
    mipmapped = _mipmapped;
    targetFormat = _targetFormat;
    preloadedPixels = std::move(pixels);
    preloadedWidth = w;
    preloadedHeight = h;
    preloadedBytesPerPixel = bytesPerPixel;
    imagePath = Core::Path{};
    declaredName = {};
    recipeSource = {};
    category = _category;
    ownerModelId = _ownerModelId;
    ownerImageIndex = _ownerImageIndex;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }

    task.taskSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void TextureGenerateSlot::Clear()
{
    imagePath = Core::Path{};
    outputPath = Core::Path{};
    sourceImage = {};
    mipData = {};
    imageStagingAllocator.Reset();
    imageReceivingAllocator.Reset();
    preloadedPixels = {};
    preloadedWidth = 0;
    preloadedHeight = 0;
    preloadedBytesPerPixel = 0;
}

void TextureGenerateSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    VkCommandBuffer cmd = taskSlot->computeSubmit.cmd;
    VkFence fence = taskSlot->computeSubmit.fence;

    auto startRecording = [&] {
        VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    };

    auto submitAndWait = [&](bool restart) {
        ZoneScopedN("SubmitAndWait");
        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        taskSlot->_dispatchCallback(cmd, fence, &done);
        done.acquire();
        VK_CHECK(vkWaitForFences(taskSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(taskSlot->context->device, 1, &fence));
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        if (restart) {
            VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
        }
    };

    bool loadRes = taskSlot->LoadImageAndGenerate(cmd, startRecording, submitAndWait);
    if (!loadRes) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        taskSlot->computeSubmit.Reset(taskSlot->context);
        return;
    }

    if (!taskSlot->WriteWTextureFile()) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        taskSlot->computeSubmit.Reset(taskSlot->context);
        return;
    }

    taskSlot->_notifyCallback(true, taskSlot->slotHandle);

    taskSlot->computeSubmit.Reset(taskSlot->context);
}

bool TextureGenerateSlot::LoadImageAndGenerate(VkCommandBuffer cmd, const Core::InlineFunction<void()>& startRecording, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("LoadImageAndGenerate");

    int32_t imgWidth, imgHeight;
    size_t pixelSize;
    stbi_uc* stbiData = nullptr;
    const void* pixelData = nullptr;

    if (preloadedPixels.IsAllocated()) {
        imgWidth = preloadedWidth;
        imgHeight = preloadedHeight;
        pixelSize = static_cast<size_t>(preloadedWidth) * preloadedHeight * preloadedBytesPerPixel;
        pixelData = preloadedPixels.Data();
    }
    else {
        int32_t w{};
        int32_t h{};
        int32_t nrChannels{};
        stbi_set_flip_vertically_on_load_thread(flipY ? 1 : 0);
        stbiData = stbi_load(imagePath.c_str(), &w, &h, &nrChannels, 4);
        stbi_set_flip_vertically_on_load_thread(0);
        if (!stbiData) {
            LOG_ERROR(Asset, "Failed to load image: {}", imagePath.c_str());
            return false;
        }
        imgWidth = w;
        imgHeight = h;
        pixelSize = static_cast<size_t>(imgWidth) * imgHeight * 4;
        pixelData = stbiData;
    }

    VkExtent3D imagesize = {static_cast<uint32_t>(imgWidth), static_cast<uint32_t>(imgHeight), 1};

    imageStagingAllocator.Reset();
    auto allocation = imageStagingAllocator.Allocate(pixelSize);
    if (allocation == SIZE_MAX) {
        LOG_ERROR(Asset, "Texture too large for staging buffer");
        if (stbiData) { stbi_image_free(stbiData); }
        return false;
    }

    Core::Array<Render::ImageView, 13> storageViews{};

    startRecording();

    char* bufferOffset = static_cast<char*>(imageStagingBuffer.allocationInfo.pMappedData) + allocation;
    memcpy(bufferOffset, pixelData, pixelSize);
    if (stbiData) {
        stbi_image_free(stbiData);
    }

    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, imagesize, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    mipLevels = mipmapped ? static_cast<uint32_t>(std::floor(std::log2(std::max(imgWidth, imgHeight)))) + 1 : 1;
    imageCreateInfo.mipLevels = mipLevels;

    sourceImage = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
        sourceImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(cmd, &depInfo);

    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = allocation;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = imagesize;

    vkCmdCopyBufferToImage(cmd, imageStagingBuffer.handle, sourceImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    sourceImage.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    if (mipmapped && mipLevels > 1) {
        ZoneScopedN("GenerateMipmaps");

        const Render::PipelineEntry resizeEntry = pipelineManager->GetPipelineEntrySnapshot("procedural_resize"_sid);
        if (resizeEntry.pipeline == VK_NULL_HANDLE) {
            LOG_ERROR(Asset, "procedural_resize pipeline not ready");
            return false;
        }

        assert(mipLevels <= 13 && "storageViews/mipData bound");
        for (uint32_t mip = 0; mip < mipLevels; mip++) {
            VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(sourceImage.handle, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
            viewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1);
            storageViews[mip] = Render::ImageView::CreateImageView(context, viewInfo);
            downsampleResources.SetRWTexture({nullptr, storageViews[mip].handle, VK_IMAGE_LAYOUT_GENERAL}, mip);
        }

        VkImageMemoryBarrier2 firstBarrier = Render::VkHelpers::ImageMemoryBarrier(
            sourceImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, sourceImage.layout,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
        );
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &firstBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        VkDescriptorBufferBindingInfoEXT binding = downsampleResources.GetBindingInfo();
        uint32_t bindingIndex = 0;
        VkDeviceSize bindingOffset = 0;
        vkCmdBindDescriptorBuffersEXT(cmd, 1, &binding);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resizeEntry.layout, 0, 1, &bindingIndex, &bindingOffset);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resizeEntry.pipeline);

        for (uint32_t mip = 1; mip < mipLevels; mip++) {
            VkImageMemoryBarrier2 barriers[2];
            uint32_t barrierCount = 0;
            if (mip > 1) {
                barriers[barrierCount++] = Render::VkHelpers::ImageMemoryBarrier(
                    sourceImage.handle,
                    Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 1),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
                );
            }
            barriers[barrierCount++] = Render::VkHelpers::ImageMemoryBarrier(
                sourceImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1),
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            depInfo.imageMemoryBarrierCount = barrierCount;
            depInfo.pImageMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(cmd, &depInfo);

            ProceduralMipDownsamplePushConstant resizePC{mip - 1, mip};
            vkCmdPushConstants(cmd, resizeEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ProceduralMipDownsamplePushConstant), &resizePC);
            const uint32_t mipWidth = static_cast<uint32_t>(std::max(1, imgWidth >> mip));
            const uint32_t mipHeight = static_cast<uint32_t>(std::max(1, imgHeight >> mip));
            vkCmdDispatch(cmd, (mipWidth + PROCEDURAL_TEXTURE_DISPATCH_X - 1) / PROCEDURAL_TEXTURE_DISPATCH_X, (mipHeight + PROCEDURAL_TEXTURE_DISPATCH_Y - 1) / PROCEDURAL_TEXTURE_DISPATCH_Y, 1);
        }

        VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
            sourceImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &finalBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
        sourceImage.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    else {
        barrier = Render::VkHelpers::ImageMemoryBarrier(
            sourceImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, sourceImage.layout,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
        sourceImage.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    submitAndWait(true);

    // Copy back to CPU
    {
        ZoneScopedN("CopyImageToCPU");
        for (uint32_t mip = 0; mip < mipLevels; mip++) {
            uint32_t mipWidth = std::max(1u, sourceImage.extent.width >> mip);
            uint32_t mipHeight = std::max(1u, sourceImage.extent.height >> mip);
            size_t mipSize = mipWidth * mipHeight * 4;

            if (mipSize > imageReceivingBuffer.allocationInfo.size) {
                LOG_ERROR(Asset, "Mip level {} too large for receiving buffer", mip);
                return false;
            }

            VkBufferImageCopy _copyRegion{};
            _copyRegion.bufferOffset = 0;
            _copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            _copyRegion.imageSubresource.mipLevel = mip;
            _copyRegion.imageSubresource.baseArrayLayer = 0;
            _copyRegion.imageSubresource.layerCount = 1;
            _copyRegion.imageExtent = {mipWidth, mipHeight, 1};

            vkCmdCopyImageToBuffer(cmd, sourceImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageReceivingBuffer.handle, 1, &_copyRegion);
            submitAndWait(mip < mipLevels - 1);

            mipData[mip] = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, mipSize);
            memcpy(mipData[mip].Data(), imageReceivingBuffer.allocationInfo.pMappedData, mipSize);
        }
    }

    return true;
}

bool TextureGenerateSlot::WriteWTextureFile()
{
    ZoneScopedN("WriteWTextureFile");

    VkFormat targetVkFormat;
    switch (targetFormat) {
        case DXGI_FORMAT_BC7_UNORM: targetVkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
            break;
        case DXGI_FORMAT_BC7_UNORM_SRGB: targetVkFormat = VK_FORMAT_BC7_SRGB_BLOCK;
            break;
        case DXGI_FORMAT_BC5_UNORM: targetVkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
            break;
        case DXGI_FORMAT_BC4_UNORM: targetVkFormat = VK_FORMAT_BC4_UNORM_BLOCK;
            break;
        default: targetVkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
            break;
    }

    const Engine::WImageDesc desc{targetVkFormat, sourceImage.extent.width, sourceImage.extent.height, mipLevels, 1};
    const size_t blobSize = Engine::WImageBlobSize(desc);
    auto blob = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, blobSize);
    if (Engine::WImageBlobInit(blob.Data(), blob.Size(), desc) == 0) {
        LOG_ERROR(Asset, "Failed to lay out texture image blob");
        return false;
    } {
        ZoneScopedN("EncodeBC");
        rdo_bc::rdo_bc_params encodeParams;
        const bool fastMode = assetGenerator->GetFastMode();
        encodeParams.m_bc7_uber_level = fastMode ? 1 : BC7_UBER_LEVEL;
        encodeParams.m_rdo_lambda = fastMode ? 0.0f : RDO_LAMBDA;
        encodeParams.m_dxgi_format = targetFormat;
        if (encodeParams.m_dxgi_format == DXGI_FORMAT_BC7_UNORM_SRGB) {
            encodeParams.m_dxgi_format = DXGI_FORMAT_BC7_UNORM;
        }

        struct EncodeMipTask : enki::ITaskSet
        {
            rdo_bc::rdo_bc_params* params;
            Core::Array<Core::HeapArray<uint8_t>, 13>* mipData;
            VkExtent3D imageExtent;
            uint8_t* blob;

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override
            {
                for (uint32_t mip = range.start; mip < range.end; ++mip) {
                    ZoneScopedN("EncodeMip");
                    uint32_t mipWidth = std::max(1u, imageExtent.width >> mip);
                    uint32_t mipHeight = std::max(1u, imageExtent.height >> mip);

                    utils::image_u8 srcImage(mipWidth, mipHeight);
                    memcpy(srcImage.get_pixels().data(), (*mipData)[mip].Data(), static_cast<size_t>(mipWidth) * mipHeight * 4);

                    rdo_bc::rdo_bc_encoder encoder;
                    if (!encoder.init(srcImage, *params)) {
                        LOG_ERROR(Asset, "GPU texture compression init failed");
                    }
                    if (!encoder.encode()) {
                        LOG_ERROR(Asset, "GPU texture compression encoding failed");
                    }

                    const void* compressedBlocks = encoder.get_blocks();
                    uint32_t blocksSizeInBytes = encoder.get_total_blocks_size_in_bytes();

                    assert(blocksSizeInBytes == Engine::WImageFaceSize(blob, mip) && "Encoder output does not match blob level size");
                    memcpy(Engine::WImageFaceData(blob, mip, 0), compressedBlocks, blocksSizeInBytes);
                }
            }
        };

        EncodeMipTask _task{};
        _task.params = &encodeParams;
        _task.mipData = &mipData;
        _task.imageExtent = sourceImage.extent;
        _task.blob = blob.Data();
        _task.m_SetSize = mipLevels;

        scheduler->AddTaskSetToPipe(&_task);
        scheduler->WaitforTask(&_task);
    }

    auto maxCompressedSize = Engine::CompressMaxSize(Engine::DEFAULT_TEXTURE_COMPRESSION, blobSize);
    auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
    size_t realCompressedSize = Engine::Compress(Engine::DEFAULT_TEXTURE_COMPRESSION, blob.Data(), blobSize, compressed.Data(), compressed.Size());

    Engine::WTextureHeader header{};
    header.textureId = textureId.id;
    header.contentVersion = contentVersion;
    header.width = sourceImage.extent.width;
    header.height = sourceImage.extent.height;
    header.mipCount = mipLevels;
    header.uncompressedSize = blobSize;
    header.dataSize = realCompressedSize;

    Core::InlineString<128> stem = declaredName;
    if (stem.Size() == 0) {
        stem = Core::InlineString<128>(imagePath.IsEmpty() ? outputPath.Stem() : imagePath.Stem());
    }
    const size_t copyLen = std::min(stem.Size(), Engine::WTEXTURE_NAME_LENGTH - 1);
    memcpy(header.name, stem.c_str(), copyLen);
    header.name[copyLen] = '\0';

    if (recipeSource.Size() > 0) {
        const size_t srcLen = std::min(recipeSource.Size(), Engine::WTEXTURE_GEN_SOURCE_LENGTH - 1);
        memcpy(header.genSource, recipeSource.c_str(), srcLen);
        header.genSource[srcLen] = '\0';
        header.genFormat = static_cast<uint32_t>(targetFormat);
        header.bGenMips = mipmapped;
        header.bGenFlipY = flipY;
    }
    header.category = category;
    header.ownerModelId = ownerModelId;
    header.ownerImageIndex = ownerImageIndex;

    Platform::CreateDirectories(outputPath.Parent().c_str());
    // Temp + rename so a crash mid-write never leaves a truncated .wtexture (an ungenerated stub survives and retries next run)
    const Core::Path tmpPath(Core::InlineString<512>::Format("%s.tmp", outputPath.c_str()).c_str());
    {
        Core::Vector<std::byte> headerOut(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
        Engine::WriteWTextureHeader(headerOut, header);
        if (!Platform::WriteFile(tmpPath, headerOut.Data(), headerOut.Size()) ||
            !Platform::AppendFile(tmpPath, compressed.Data(), realCompressedSize)) {
            LOG_ERROR(Asset, "Failed to write output file: {}", tmpPath.c_str());
            return false;
        }
    }
    if (!Platform::RenameFile(tmpPath, outputPath)) {
        LOG_ERROR(Asset, "Failed to move {} into place", tmpPath.c_str());
        return false;
    }

    LOG_INFO(Asset, "Wrote {}", outputPath.c_str());
    return true;
}
} // Editor
