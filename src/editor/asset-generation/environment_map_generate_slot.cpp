//
// Created by William on 2026-02-09.
//

#include "environment_map_generate_slot.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <ktx.h>
#include <tracy/Tracy.hpp>

#include "asset_generation_types.h"
#include "engine/compression/compression.h"
#include "engine/resources/environment_map/environment_map_format.h"
#include "platform/file_utils.h"
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
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::PipelineManager* _pipelineManager,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
    Core::InlineFunction<void(bool success, EnvironmentMapGenerateSlotHandle cubemapSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    pipelineManager = _pipelineManager;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
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
    const Core::Path& _imagePath,
    const Core::Path& _outputPath,
    Engine::EnvironmentMapID _environmentMapId)
{
    slotHandle = _slotHandle;
    imagePath = _imagePath;
    outputPath = _outputPath;
    environmentMapId = _environmentMapId;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }

    task.taskSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void EnvironmentMapGenerateSlot::Clear()
{
    imagePath = Core::Path{};
    outputPath = Core::Path{};
    equiImage = {};
    equiImageView = {};
    mipmappedCubemapImage = {};
    mipmappedCubemapImageView = {};
    finalCubemapImage = {};
    for (auto& imgView : finalCubemapMipViews) {
        imgView = {};
    }
    mipData = {};
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

    if (!taskSlot->WriteWEnvMapFile()) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
        vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
        return;
    }

    taskSlot->_notifyCallback(true, taskSlot->slotHandle);

    vkDestroyFence(taskSlot->context->device, graphicsFence, nullptr);
    vkDestroyCommandPool(taskSlot->context->device, graphicsCommandPool, nullptr);
}

bool EnvironmentMapGenerateSlot::LoadEquirectangularAndGenerate(VkCommandBuffer cmd, const Core::InlineFunction<void()>& startRecording, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("LoadEquirectangularAndGenerate");

    // Load HDR equirectangular image
    int32_t width, height, nrChannels;
    float* hdrData = stbi_loadf(imagePath.c_str(), &width, &height, &nrChannels, 4);
    if (!hdrData) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to load HDR image: {}", imagePath.c_str());
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

    const uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(ENVIRONMENT_MAP_RESOLUTION))) + 1;

    VkImageCreateInfo cubemapCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        {ENVIRONMENT_MAP_RESOLUTION, ENVIRONMENT_MAP_RESOLUTION, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    cubemapCreateInfo.arrayLayers = 6;
    cubemapCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cubemapCreateInfo.mipLevels = mipLevels;
    mipmappedCubemapImage = Render::AllocatedImage::CreateAllocatedImage(context, cubemapCreateInfo);
    VkImageViewCreateInfo cubemapViewInfo = Render::VkHelpers::ImageViewCreateInfo(
        mipmappedCubemapImage.handle,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    cubemapViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubemapViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6);
    mipmappedCubemapImageView = Render::ImageView::CreateImageView(context, cubemapViewInfo);

    success = resourceManager->environmentMapGenerateResources.SetCubemap({nullptr, mipmappedCubemapImageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, 0);
    assert(success);
    success = resourceManager->environmentMapGenerateResources.SetRWCubemapFloatArray({nullptr, mipmappedCubemapImageView.handle, VK_IMAGE_LAYOUT_GENERAL}, 0);
    assert(success);

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        mipmappedCubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Generate mip 0 cubemap
    {
        ZoneScopedN("Generate Cubemap Mip 0")
        EquirectToCubemapPushConstant eqPc{};
        eqPc.samplerIndex = EQUI_IMAGE_SAMPLER_INDEX;
        eqPc.sourceEquiIndex = 0;
        eqPc.targetCubeIndex = 0;
        eqPc.cubemapWidth = 1024;
        eqPc.cubemapHeight = 1024;


        Core::Array<VkDescriptorBufferBindingInfoEXT, 1> bindings{resourceManager->environmentMapGenerateResources.GetBindingInfo()};
        uint32_t bindingIndex{0u};
        VkDeviceSize bindingOffset{0};
        vkCmdBindDescriptorBuffersEXT(cmd, bindings.Size(), bindings.Data());

        const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ibl_equirect_to_cubemap"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(eqPc), &eqPc);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);
        uint32_t dispatchX = (ENVIRONMENT_MAP_RESOLUTION + ENVIRONMENT_MAP_GENERATION_DISPATCH_X - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_X;
        uint32_t dispatchY = (ENVIRONMENT_MAP_RESOLUTION + ENVIRONMENT_MAP_GENERATION_DISPATCH_Y - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_Y;
        vkCmdDispatch(cmd, dispatchX, dispatchY, 6);
        submitAndWait(true);
    }

    // Cubemap mipmap generation
    {
        ZoneScopedN("GenerateCubemapMipmaps");

        barrier = Render::VkHelpers::ImageMemoryBarrier(
            mipmappedCubemapImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);

        barrier = Render::VkHelpers::ImageMemoryBarrier(
            mipmappedCubemapImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, mipLevels - 1, 0, 6),
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);

        uint32_t mipWidth = ENVIRONMENT_MAP_RESOLUTION;
        uint32_t mipHeight = ENVIRONMENT_MAP_RESOLUTION;

        for (uint32_t mip = 1; mip < mipLevels; mip++) {
            uint32_t nextWidth = mipWidth / 2;
            uint32_t nextHeight = mipHeight / 2;

            for (uint32_t face = 0; face < 6; face++) {
                VkImageBlit2 blitRegion{.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
                blitRegion.srcOffsets[0] = {0, 0, 0};
                blitRegion.srcOffsets[1] = {static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight), 1};
                blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blitRegion.srcSubresource.mipLevel = mip - 1;
                blitRegion.srcSubresource.baseArrayLayer = face;
                blitRegion.srcSubresource.layerCount = 1;

                blitRegion.dstOffsets[0] = {0, 0, 0};
                blitRegion.dstOffsets[1] = {static_cast<int32_t>(nextWidth), static_cast<int32_t>(nextHeight), 1};
                blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blitRegion.dstSubresource.mipLevel = mip;
                blitRegion.dstSubresource.baseArrayLayer = face;
                blitRegion.dstSubresource.layerCount = 1;

                VkBlitImageInfo2 blitInfo{.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
                blitInfo.srcImage = mipmappedCubemapImage.handle;
                blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                blitInfo.dstImage = mipmappedCubemapImage.handle;
                blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                blitInfo.regionCount = 1;
                blitInfo.pRegions = &blitRegion;
                blitInfo.filter = VK_FILTER_LINEAR;

                vkCmdBlitImage2(cmd, &blitInfo);
            }

            if (mip < mipLevels - 1) {
                barrier = Render::VkHelpers::ImageMemoryBarrier(
                    mipmappedCubemapImage.handle,
                    Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6),
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                );
                vkCmdPipelineBarrier2(cmd, &depInfo);
            }

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        barrier = Render::VkHelpers::ImageMemoryBarrier(
            mipmappedCubemapImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels - 1, 0, 6),
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);

        // Transition last mip (currently in DST)
        barrier = Render::VkHelpers::ImageMemoryBarrier(
            mipmappedCubemapImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 6),
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    VkImageCreateInfo finalCubemapCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        {ENVIRONMENT_MAP_RESOLUTION, ENVIRONMENT_MAP_RESOLUTION, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );
    finalCubemapCreateInfo.arrayLayers = 6;
    finalCubemapCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    finalCubemapCreateInfo.mipLevels = ENVIRONMENT_MAP_MIPS;
    finalCubemapImage = Render::AllocatedImage::CreateAllocatedImage(context, finalCubemapCreateInfo);


    for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_MIPS; mip++) {
        VkImageViewCreateInfo mipViewInfo = Render::VkHelpers::ImageViewCreateInfo(
            finalCubemapImage.handle,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        mipViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        mipViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6);
        finalCubemapMipViews[mip] = Render::ImageView::CreateImageView(context, mipViewInfo);
        success = resourceManager->environmentMapGenerateResources.SetRWCubemapHalfArray(
            {nullptr, finalCubemapMipViews[mip].handle, VK_IMAGE_LAYOUT_GENERAL},
            mip
        );
        assert(success);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        finalCubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, ENVIRONMENT_MAP_MIPS, 0, 6),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    Core::Array<VkDescriptorBufferBindingInfoEXT, 1> bindings{resourceManager->environmentMapGenerateResources.GetBindingInfo()};
    uint32_t bindingIndex{0u};
    VkDeviceSize bindingOffset{0};
    vkCmdBindDescriptorBuffersEXT(cmd, bindings.Size(), bindings.Data());

    // Generate specular prefilter for mips 0-4
    {
        ZoneScopedN("PrefilterSpecular");
        const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ibl_prefilter_specular"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);

        for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_DIFFUSE_MIP; mip++) {
            float roughness = static_cast<float>(mip) / static_cast<float>(ENVIRONMENT_MAP_DIFFUSE_MIP - 1);
            uint32_t mipResolution = ENVIRONMENT_MAP_RESOLUTION >> mip;

            PrefilterSpecularPushConstant pc{
                .samplerIndex = CUBEMAP_IMAGE_SAMPLER_INDEX,
                .sourceIndex = 0, // mipmapped cubemap index (0, float4 sampler descriptor)
                .targetIndex = mip, // final cubemap index (half4 storage descriptor)
                .roughness = roughness,
                .width = mipResolution,
                .height = mipResolution,
                .sampleCount = 4096,
                .fireflyThreshold = 10.0f
            };

            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t dispatchX = (mipResolution + ENVIRONMENT_MAP_GENERATION_DISPATCH_X - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_X;
            uint32_t dispatchY = (mipResolution + ENVIRONMENT_MAP_GENERATION_DISPATCH_Y - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_Y;
            vkCmdDispatch(cmd, dispatchX, dispatchY, 6);
        }
    }

    // Generate diffuse irradiance for mip 5
    {
        ZoneScopedN("ConvolveDiffuse");
        const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ibl_convolve_diffuse"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);

        ConvolveDiffusePushConstant pc{
            .samplerIndex = CUBEMAP_IMAGE_SAMPLER_INDEX,
            .sourceIndex = 0, // mipmapped cubemap index
            .targetIndex = ENVIRONMENT_MAP_DIFFUSE_MIP,
            .targetWidth = ENVIRONMENT_MAP_DIFFUSE_RESOLUTION,
            .targetHeight = ENVIRONMENT_MAP_DIFFUSE_RESOLUTION,
            .sampleDelta = 0.025f
        };

        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t dispatchX = (ENVIRONMENT_MAP_DIFFUSE_RESOLUTION + ENVIRONMENT_MAP_GENERATION_DISPATCH_X - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_X;
        uint32_t dispatchY = (ENVIRONMENT_MAP_DIFFUSE_RESOLUTION + ENVIRONMENT_MAP_GENERATION_DISPATCH_Y - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_Y;
        vkCmdDispatch(cmd, dispatchX, dispatchY, 6);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        finalCubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, ENVIRONMENT_MAP_MIPS, 0, 6),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Copy all mip faces back to CPU for KTX generation
    {
        ZoneScopedN("CopyCubemapToCPU");

        for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_MIPS; mip++) {
            uint32_t mipResolution = (mip < ENVIRONMENT_MAP_DIFFUSE_MIP) ? (ENVIRONMENT_MAP_RESOLUTION >> mip) : ENVIRONMENT_MAP_DIFFUSE_RESOLUTION;
            size_t faceSize = mipResolution * mipResolution * 4 * sizeof(uint16_t);

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

                vkCmdCopyImageToBuffer(cmd, finalCubemapImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageReceivingBuffer.handle, 1, &_copyRegion);
                submitAndWait(!(mip == ENVIRONMENT_MAP_MIPS - 1 && face == 5));

                mipData[mip][face] = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetModel, faceSize);
                memcpy(mipData[mip][face].Data(), imageReceivingBuffer.allocationInfo.pMappedData, faceSize);
            }
        }
    }

    return true;
}

bool EnvironmentMapGenerateSlot::WriteWEnvMapFile()
{
    ZoneScopedN("WriteWEnvMapFile");

    ktxTexture2* texture;
    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    createInfo.baseWidth = ENVIRONMENT_MAP_RESOLUTION;
    createInfo.baseHeight = ENVIRONMENT_MAP_RESOLUTION;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = ENVIRONMENT_MAP_MIPS;
    createInfo.numLayers = 1;
    createInfo.numFaces = 6;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to create KTX texture");
        return false;
    }
    for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_MIPS; mip++) {
        for (uint32_t face = 0; face < 6; face++) {
            ktxTexture_SetImageFromMemory(ktxTexture(texture), mip, 0, face, mipData[mip][face].Data(), mipData[mip][face].Size());
        }
    }

    ktx_uint8_t* ktxBytes{nullptr};
    ktx_size_t ktxSize{0};
    result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
    ktxTexture_Destroy(ktxTexture(texture));

    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to serialise KTX texture to memory");
        return false;
    }

    auto maxCompressedSize = Engine::CompressMaxSize(Engine::DEFAULT_ENV_MAP_COMPRESSION, ktxSize);
    auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
    size_t realCompressedSize = Engine::Compress(Engine::DEFAULT_ENV_MAP_COMPRESSION, ktxBytes, ktxSize, compressed.Data(), compressed.Size());
    free(ktxBytes);

    Engine::WEnvMapHeader header{};
    header.environmentMapId = environmentMapId.id;
    header.width = ENVIRONMENT_MAP_RESOLUTION;
    header.height = ENVIRONMENT_MAP_RESOLUTION;
    header.mipCount = ENVIRONMENT_MAP_MIPS;
    header.uncompressedSize = ktxSize;
    header.dataSize = realCompressedSize;

    Core::InlineString stem{imagePath.IsEmpty() ? Core::InlineString(outputPath.Stem()) : Core::InlineString(imagePath.Stem())};
    const size_t copyLen = std::min(stem.Size(), Engine::WENVMAP_NAME_LENGTH - 1);
    memcpy(header.name, stem.c_str(), copyLen);
    header.name[copyLen] = '\0';

    Platform::CreateDirectories(outputPath.Parent().c_str());
    std::ofstream f(outputPath.c_str(), std::ios::binary);
    if (!f) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to open output file: {}", outputPath.c_str());
        return false;
    }

    if (!Engine::WriteWEnvMapHeader(f, header)) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to write header: {}", outputPath.c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(compressed.Data()), static_cast<std::streamsize>(realCompressedSize));

    SPDLOG_INFO("[EnvironmentMapGenerateSlot] Wrote {}", outputPath.c_str());
    return true;
}
} // namespace Editor
