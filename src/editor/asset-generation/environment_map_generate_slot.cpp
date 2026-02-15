//
// Created by William on 2026-02-09.
//

#include "environment_map_generate_slot.h"

#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <ktx.h>
#include <tracy/Tracy.hpp>

#include "asset_generation_types.h"
#include "platform/paths.h"
#include "render/resource_manager.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace Editor
{
EnvironmentMapGenerateSlot::EnvironmentMapGenerateSlot() = default;

EnvironmentMapGenerateSlot::~EnvironmentMapGenerateSlot() = default;

void EnvironmentMapGenerateSlot::Initialize(
    int32_t slotIndex,
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::PipelineManager* _pipelineManager,
    Render::ResourceManager* _resourceManager,
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
    std::function<void(bool success, EnvironmentMapGenerateSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    pipelineManager = _pipelineManager;
    resourceManager = _resourceManager;
    temporaryPath = Platform::GetExecutablePath() / "temp" / ("envmap_gen_" + std::to_string(slotIndex));
    _graphicsDispatchCallback = std::move(graphicsDispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    imageStagingBuffer = Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE);
    imageReceivingBuffer = Render::AllocatedBuffer::CreateAllocatedReceivingBuffer(context, ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE);

    VkSamplerCreateInfo equiSamplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };

    equiSampler = Render::Sampler::CreateSampler(context, equiSamplerInfo);

    VkSamplerCreateInfo cubemapSamplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };
    cubemapSampler = Render::Sampler::CreateSampler(context, cubemapSamplerInfo);


    resourceManager->environmentMapGenerateResources.SetSampler(equiSampler.handle, EQUI_IMAGE_SAMPLER_INDEX);
    resourceManager->environmentMapGenerateResources.SetSampler(cubemapSampler.handle, CUBEMAP_IMAGE_SAMPLER_INDEX);
}

void EnvironmentMapGenerateSlot::Launch(
    EnvironmentMapGenerateSlotHandle _slotHandle,
    const std::filesystem::path& _imagePath,
    const std::filesystem::path& _outputPath)
{
    slotHandle = _slotHandle;
    imagePath = _imagePath;
    outputPath = _outputPath;

    if (task && !task->GetIsComplete()) {
        scheduler->WaitforTask(task.get());
    }

    task = std::make_unique<GenerateTask>();
    task->taskSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void EnvironmentMapGenerateSlot::Clear()
{
    imagePath.clear();
    outputPath.clear();
    cubemapImage = {};
    equiImage = {};
    mipData.clear();
    imageStagingAllocator.Reset();
    imageReceivingAllocator.Reset();
}

void EnvironmentMapGenerateSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
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

    bool loadRes = taskSlot->LoadEquirectangularAndGenerate(graphicsCmd, startGraphicsRecording, graphicsSubmitAndWait);
    if (!loadRes) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
        vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
        return;
    }

    if (!taskSlot->WriteKTXFile()) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
        vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
        return;
    }

    taskSlot->_notifyCallback(true, taskSlot->slotHandle);

    vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
    vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
}

bool EnvironmentMapGenerateSlot::LoadEquirectangularAndGenerate(
    VkCommandBuffer cmd,
    const std::function<void()>& startRecording,
    const std::function<void(bool)>& submitAndWait)
{
    ZoneScopedN("LoadEquirectangularAndGenerate");

    // Load HDR equirectangular image
    int32_t width, height, nrChannels;
    float* hdrData = stbi_loadf(imagePath.string().c_str(), &width, &height, &nrChannels, 4);
    if (!hdrData) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to load HDR image: {}", imagePath.string());
        return false;
    }

    VkExtent3D equirectSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    const size_t hdrSize = width * height * 4 * sizeof(float);

    imageStagingAllocator.Reset();
    auto allocation = imageStagingAllocator.Allocate(hdrSize);
    if (allocation == SIZE_MAX) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] HDR image too large for staging buffer");
        stbi_image_free(hdrData);
        return false;
    }

    startRecording();

    char* bufferOffset = static_cast<char*>(imageStagingBuffer.allocationInfo.pMappedData) + allocation;
    memcpy(bufferOffset, hdrData, hdrSize);
    stbi_image_free(hdrData);

    VkImageCreateInfo equirectCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        equirectSize,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    equiImage = Render::AllocatedImage::CreateAllocatedImage(context, equirectCreateInfo);

    VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
        equiImage.handle,
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
    copyRegion.imageExtent = equirectSize;

    vkCmdCopyBufferToImage(cmd, imageStagingBuffer.handle, equiImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        equiImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    VkImageViewCreateInfo equiViewInfo = Render::VkHelpers::ImageViewCreateInfo(
        equiImage.handle,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    equiViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    equiImageView = std::move(Render::ImageView::CreateImageView(context, equiViewInfo));
    bool success = resourceManager->environmentMapGenerateResources.SetTexture2D({nullptr, equiImageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, 0);
    assert(success);

    // Create cubemap image with 6 mips (5 specular + 1 diffuse)
    VkImageCreateInfo cubemapCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        {Render::ENVIRONMENT_MAP_RESOLUTION, Render::ENVIRONMENT_MAP_RESOLUTION, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );
    cubemapCreateInfo.arrayLayers = 6;
    cubemapCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cubemapCreateInfo.mipLevels = TOTAL_MIPS;

    cubemapImage = Render::AllocatedImage::CreateAllocatedImage(context, cubemapCreateInfo);
    VkImageViewCreateInfo cubemapViewInfo = Render::VkHelpers::ImageViewCreateInfo(
        cubemapImage.handle,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    cubemapViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubemapViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, TOTAL_MIPS, 0, 6);
    cubemapImageView = Render::ImageView::CreateImageView(context, cubemapViewInfo);

    success = resourceManager->environmentMapGenerateResources.SetRWCubemapArray({nullptr, cubemapImageView.handle, VK_IMAGE_LAYOUT_GENERAL}, 0);
    assert(success);

    // Transition all mips to GENERAL for compute writes
    barrier = Render::VkHelpers::ImageMemoryBarrier(
        cubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, TOTAL_MIPS, 0, 6),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    EquirectToCubemapPushConstant eqPc;
    eqPc.samplerIndex = EQUI_IMAGE_SAMPLER_INDEX;
    eqPc.sourceEquiIndex = 0;
    eqPc.targetCubeIndex = 0;
    eqPc.cubemapWidth = 1024;
    eqPc.cubemapHeight = 1024;

    std::array bindings{resourceManager->environmentMapGenerateResources.GetBindingInfo()};
    uint32_t bindingIndex{0u};
    VkDeviceSize bindingOffset{0};
    vkCmdBindDescriptorBuffersEXT(cmd, bindings.size(), bindings.data());

    const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("ibl_equirect_to_cubemap");
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(eqPc), &eqPc);
    vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.size(), &bindingIndex, &bindingOffset);
    vkCmdDispatch(cmd, Render::ENVIRONMENT_MAP_RESOLUTION / 16, Render::ENVIRONMENT_MAP_RESOLUTION / 16, 6);
    submitAndWait(true);
    // todo: generate specular and diffuse

    // Copy all mip faces back to CPU for KTX generation
    {
        ZoneScopedN("CopyCubemapToCPU");
        mipData.resize(TOTAL_MIPS);

        barrier = Render::VkHelpers::ImageMemoryBarrier(
            cubemapImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, TOTAL_MIPS, 0, 6),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);

        for (uint32_t mip = 0; mip < TOTAL_MIPS; mip++) {
            uint32_t mipResolution = (mip < DIFFUSE_MIP) ? (Render::ENVIRONMENT_MAP_RESOLUTION >> mip) : Render::ENVIRONMENT_MAP_DIFFUSE_RESOLUTION;
            size_t faceSize = mipResolution * mipResolution * 4 * sizeof(float);

            if (faceSize > imageReceivingBuffer.allocationInfo.size) {
                SPDLOG_ERROR("Mip {} face too large for receiving buffer", mip);
                return false;
            }

            for (uint32_t face = 0; face < 6; face++) {
                VkBufferImageCopy _copyRegion{};
                _copyRegion.bufferOffset = 0;
                _copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                _copyRegion.imageSubresource.mipLevel = mip;
                _copyRegion.imageSubresource.baseArrayLayer = face;
                _copyRegion.imageSubresource.layerCount = 1;
                _copyRegion.imageExtent = {mipResolution, mipResolution, 1};

                vkCmdCopyImageToBuffer(cmd, cubemapImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageReceivingBuffer.handle, 1, &_copyRegion);
                submitAndWait(!(mip == TOTAL_MIPS - 1 && face == 5));

                mipData[mip][face].resize(faceSize);
                memcpy(mipData[mip][face].data(), imageReceivingBuffer.allocationInfo.pMappedData, faceSize);
            }
        }
    }

    //todo: Add an additional final step that converts 32bit->16bit and save that 16 bit (half4 in slang)

    return true;
}

bool EnvironmentMapGenerateSlot::WriteKTXFile()
{
    ZoneScopedN("WriteKTXFile");

    ktxTexture2* texture;
    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    createInfo.baseWidth = Render::ENVIRONMENT_MAP_RESOLUTION;
    createInfo.baseHeight = Render::ENVIRONMENT_MAP_RESOLUTION;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = TOTAL_MIPS;
    createInfo.numLayers = 1;
    createInfo.numFaces = 6;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to create KTX texture");
        return false;
    }
    for (uint32_t mip = 0; mip < TOTAL_MIPS; mip++) {
        for (uint32_t face = 0; face < 6; face++) {
            ktxTexture_SetImageFromMemory(ktxTexture(texture), mip, 0, face, mipData[mip][face].data(), mipData[mip][face].size());
        }
    }

    result = ktxTexture_WriteToNamedFile(ktxTexture(texture), outputPath.string().c_str());
    ktxTexture_Destroy(ktxTexture(texture));

    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to write KTX file");
        return false;
    }

    SPDLOG_INFO("[EnvironmentMapGenerateSlot] Wrote {}", outputPath.string());
    return true;
}
} // namespace Editor
