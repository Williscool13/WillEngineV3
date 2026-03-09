//
// Created by William on 2026-02-03.
//

#include "texture_generate_slot.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <ktx.h>
#include <tracy/Tracy.hpp>

#include "asset_generation_types.h"
#include "bc7enc_rdo/rdo_bc_encoder.h"
#include "engine/textures/texture_format.h"
#include "platform/paths.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace Editor
{

TextureGenerateSlot::TextureGenerateSlot() = default;
TextureGenerateSlot::~TextureGenerateSlot() = default;

void TextureGenerateSlot::Initialize(
    int32_t slotIndex,
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
    std::function<void(bool success, TextureGenerateSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    temporaryPath = Platform::GetExecutablePath() / "temp" / ("texture_gen_" + std::to_string(slotIndex));
    _graphicsDispatchCallback = std::move(graphicsDispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    imageStagingBuffer = Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, TEXTURE_GENERATION_STAGING_BUFFER_SIZE);
    imageReceivingBuffer = Render::AllocatedBuffer::CreateAllocatedReceivingBuffer(context, TEXTURE_GENERATION_STAGING_BUFFER_SIZE);
}

void TextureGenerateSlot::Launch(TextureGenerateSlotHandle _slotHandle, const std::filesystem::path& _imagePath, const std::filesystem::path& _outputPath, Engine::TextureID _textureId, bool _mipmapped, DXGI_FORMAT _targetFormat)
{
    slotHandle = _slotHandle;
    imagePath = _imagePath;
    outputPath = _outputPath;
    textureId = _textureId;
    mipmapped = _mipmapped;
    targetFormat = _targetFormat;

    if (task && !task->GetIsComplete()) {
        scheduler->WaitforTask(task.get());
    }

    task = std::make_unique<GenerateTask>();
    task->taskSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void TextureGenerateSlot::Clear()
{
    imagePath.clear();
    outputPath.clear();
    sourceImage = {};
    mipData.clear();
    imageStagingAllocator.Reset();
    imageReceivingAllocator.Reset();
}

void TextureGenerateSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    VkCommandPoolCreateInfo graphicsPoolInfo = Render::VkHelpers::CommandPoolCreateInfo(taskSlot->context->graphicsQueueFamily);
    VkCommandPool graphicsCommandPool;
    VK_CHECK(vkCreateCommandPool(taskSlot->context->device, &graphicsPoolInfo, nullptr, &graphicsCommandPool));

    VkCommandBufferAllocateInfo graphicsCmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, graphicsCommandPool);
    VkCommandBuffer graphicsCmd;
    VK_CHECK(vkAllocateCommandBuffers(taskSlot->context->device, &graphicsCmdInfo, &graphicsCmd));

    VkFenceCreateInfo graphicsFenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence graphicsFence;
    VK_CHECK(vkCreateFence(taskSlot->context->device, &graphicsFenceInfo, nullptr, &graphicsFence));

    auto startGraphicsRecording = [&] {
        VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &beginInfo));
    };

    auto graphicsSubmitAndWait = [&](bool restart) {
        ZoneScopedN("GraphicsSubmitAndWait");
        VK_CHECK(vkEndCommandBuffer(graphicsCmd));
        std::binary_semaphore done(0);
        taskSlot->_graphicsDispatchCallback(graphicsCmd, graphicsFence, &done);
        done.acquire();
        VK_CHECK(vkResetFences(taskSlot->context->device, 1, &graphicsFence));
        VK_CHECK(vkResetCommandBuffer(graphicsCmd, 0));

        if (restart) {
            VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &beginInfo));
        }
    };

    bool loadRes = taskSlot->LoadImageAndGenerate(graphicsCmd, startGraphicsRecording, graphicsSubmitAndWait);
    if (!loadRes) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
        vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
        return;
    }

    if (!taskSlot->WriteWTextureFile()) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
        vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
        return;
    }

    taskSlot->_notifyCallback(true, taskSlot->slotHandle);

    vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
    vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
}

bool TextureGenerateSlot::LoadImageAndGenerate(VkCommandBuffer cmd, const std::function<void()>& startRecording, const std::function<void(bool)>& submitAndWait)
{
    ZoneScopedN("LoadImageAndGenerate");

    int32_t width, height, nrChannels;
    stbi_uc* stbiData = stbi_load(imagePath.string().c_str(), &width, &height, &nrChannels, 4);
    if (!stbiData) {
        SPDLOG_ERROR("[TextureGenerateSlot] Failed to load image: {}", imagePath.string());
        return false;
    }

    VkExtent3D imagesize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    const size_t size = width * height * 4;

    imageStagingAllocator.Reset();
    auto allocation = imageStagingAllocator.Allocate(size);
    if (allocation == SIZE_MAX) {
        SPDLOG_ERROR("[TextureGenerateSlot] Texture too large for staging buffer");
        stbi_image_free(stbiData);
        return false;
    }

    startRecording();

    char* bufferOffset = static_cast<char*>(imageStagingBuffer.allocationInfo.pMappedData) + allocation;
    memcpy(bufferOffset, stbiData, size);
    stbi_image_free(stbiData);

    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(VK_FORMAT_R8G8B8A8_UNORM, imagesize, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    mipLevels = mipmapped ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
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

        VkImageMemoryBarrier2 firstBarrier = Render::VkHelpers::ImageMemoryBarrier(
            sourceImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, sourceImage.layout,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &firstBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        for (uint32_t mip = 1; mip < mipLevels; mip++) {
            VkImageMemoryBarrier2 barriers[2];
            barriers[0] = Render::VkHelpers::ImageMemoryBarrier(
                sourceImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 1),
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            barriers[1] = Render::VkHelpers::ImageMemoryBarrier(
                sourceImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1),
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );

            depInfo.imageMemoryBarrierCount = 2;
            depInfo.pImageMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(cmd, &depInfo);

            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {(width >> (mip - 1)), (height >> (mip - 1)), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {(width >> mip), (height >> mip), 1};

            vkCmdBlitImage(cmd, sourceImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           sourceImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }

        VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
            sourceImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1),
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &finalBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
        sourceImage.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    } else {
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
        mipData.resize(mipLevels);
        for (uint32_t mip = 0; mip < mipLevels; mip++) {
            uint32_t mipWidth = std::max(1u, sourceImage.extent.width >> mip);
            uint32_t mipHeight = std::max(1u, sourceImage.extent.height >> mip);
            size_t mipSize = mipWidth * mipHeight * 4;

            if (mipSize > imageReceivingBuffer.allocationInfo.size) {
                SPDLOG_ERROR("Mip level {} too large for receiving buffer", mip);
                return false;
            }

            VkBufferImageCopy _copyRegion{};
            _copyRegion.bufferOffset = 0;
            _copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            _copyRegion.imageSubresource.mipLevel = mip;
            _copyRegion.imageSubresource.baseArrayLayer = 0;
            _copyRegion.imageSubresource.layerCount = 1;
            _copyRegion.imageExtent = {mipWidth, mipHeight, 1};

            vkCmdCopyImageToBuffer(cmd, sourceImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,imageReceivingBuffer.handle, 1, &_copyRegion);
            submitAndWait(mip < mipLevels - 1);

            mipData[mip].resize(mipSize);
            memcpy(mipData[mip].data(), imageReceivingBuffer.allocationInfo.pMappedData, mipSize);
        }
    }

    return true;
}

bool TextureGenerateSlot::WriteWTextureFile()
{
    ZoneScopedN("WriteWTextureFile");

    VkFormat targetVkFormat;
    switch (targetFormat) {
        case DXGI_FORMAT_BC7_UNORM: targetVkFormat = VK_FORMAT_BC7_UNORM_BLOCK; break;
        case DXGI_FORMAT_BC7_UNORM_SRGB: targetVkFormat = VK_FORMAT_BC7_SRGB_BLOCK; break;
        case DXGI_FORMAT_BC5_UNORM: targetVkFormat = VK_FORMAT_BC5_UNORM_BLOCK; break;
        case DXGI_FORMAT_BC4_UNORM: targetVkFormat = VK_FORMAT_BC4_UNORM_BLOCK; break;
        default: targetVkFormat = VK_FORMAT_BC7_UNORM_BLOCK; break;
    }

    ktxTexture2* texture;
    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = targetVkFormat;
    createInfo.baseWidth = sourceImage.extent.width;
    createInfo.baseHeight = sourceImage.extent.height;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = mipLevels;
    createInfo.numLayers = 1;
    createInfo.numFaces = 1;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[TextureGenerateSlot] Failed to create KTX texture");
        return false;
    }

    {
        ZoneScopedN("EncodeBC");
        rdo_bc::rdo_bc_params encodeParams;
        encodeParams.m_bc7_uber_level = 1;
        encodeParams.m_rdo_lambda = 0.0f;
        encodeParams.m_dxgi_format = targetFormat;
        if (encodeParams.m_dxgi_format == DXGI_FORMAT_BC7_UNORM_SRGB) {
            encodeParams.m_dxgi_format = DXGI_FORMAT_BC7_UNORM;
        }

        struct EncodeMipTask : enki::ITaskSet
        {
            rdo_bc::rdo_bc_params* params;
            std::vector<std::vector<uint8_t>>* mipData;
            VkExtent3D imageExtent;
            ktxTexture2* texture;

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override
            {
                for (uint32_t mip = range.start; mip < range.end; ++mip) {
                    uint32_t mipWidth = std::max(1u, imageExtent.width >> mip);
                    uint32_t mipHeight = std::max(1u, imageExtent.height >> mip);

                    utils::image_u8 srcImage(mipWidth, mipHeight);
                    const uint8_t* rgbaData = (*mipData)[mip].data();
                    for (uint32_t y = 0; y < mipHeight; ++y) {
                        for (uint32_t x = 0; x < mipWidth; ++x) {
                            const uint8_t* pixel = &rgbaData[(y * mipWidth + x) * 4];
                            srcImage(x, y).set(pixel[0], pixel[1], pixel[2], pixel[3]);
                        }
                    }

                    rdo_bc::rdo_bc_encoder encoder;
                    if (!encoder.init(srcImage, *params)) {
                        SPDLOG_ERROR("[TextureGenerateSlot] GPU texture compression init failed");
                    }
                    if (!encoder.encode()) {
                        SPDLOG_ERROR("[TextureGenerateSlot] GPU texture compression encoding failed");
                    }

                    const void* compressedBlocks = encoder.get_blocks();
                    uint32_t blocksSizeInBytes = encoder.get_total_blocks_size_in_bytes();

                    ktxTexture_SetImageFromMemory(ktxTexture(texture), mip, 0, 0,
                                                  static_cast<const ktx_uint8_t*>(compressedBlocks), blocksSizeInBytes);
                }
            }
        };

        EncodeMipTask _task{};
        _task.params = &encodeParams;
        _task.mipData = &mipData;
        _task.imageExtent = sourceImage.extent;
        _task.texture = texture;
        _task.m_SetSize = mipLevels;

        scheduler->AddTaskSetToPipe(&_task);
        scheduler->WaitforTask(&_task);
    }

    const char writer[] = "WillEngine";
    ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

    ktx_uint8_t* ktxBytes{nullptr};
    ktx_size_t ktxSize{0};
    result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
    ktxTexture_Destroy(ktxTexture(texture));

    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[TextureGenerateSlot] Failed to serialise KTX texture to memory");
        return false;
    }

    Engine::WTextureHeader header{};
    memcpy(header.magic, Engine::WILL_TEXTURE_MAGIC, sizeof(header.magic));
    header.majorVersion = Engine::TEXTURE_MAJOR_VERSION;
    header.minorVersion = Engine::TEXTURE_MINOR_VERSION;
    header.textureId    = textureId.id;
    header.width        = sourceImage.extent.width;
    header.height       = sourceImage.extent.height;
    header.mipCount     = mipLevels;
    header.dataOffset   = static_cast<uint32_t>(sizeof(Engine::WTextureHeader));
    header.dataSize     = static_cast<uint64_t>(ktxSize);

    const std::string stem = imagePath.stem().string();
    const size_t copyLen = std::min(stem.size(), Engine::WTEXTURE_NAME_LENGTH - 1);
    memcpy(header.name, stem.c_str(), copyLen);
    header.name[copyLen] = '\0';

    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream f(outputPath, std::ios::binary);
    if (!f) {
        free(ktxBytes);
        SPDLOG_ERROR("[TextureGenerateSlot] Failed to open output file: {}", outputPath.string());
        return false;
    }

    f.write(reinterpret_cast<const char*>(&header), sizeof(header));
    f.write(reinterpret_cast<const char*>(ktxBytes), static_cast<std::streamsize>(ktxSize));
    free(ktxBytes);

    SPDLOG_INFO("[TextureGenerateSlot] Wrote {}", outputPath.string());
    return true;
}

} // Editor