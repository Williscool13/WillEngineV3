//
// Created by William on 2026-02-09.
//

#include "environment_map_generate_slot.h"

#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <tracy/Tracy.hpp>

#include "asset_generation_types.h"
#include "engine/compression/compression.h"
#include "engine/resources/environment_map/environment_map_format.h"
#include "engine/resources/environment_map/probe_format.h"
#include "engine/resources/wimage_format.h"
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

EnvironmentMapGenerateSlot::~EnvironmentMapGenerateSlot()
{
    computeSubmit.Destroy(context);
}

void EnvironmentMapGenerateSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::PipelineManager* _pipelineManager,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, EnvironmentMapGenerateSlotHandle cubemapSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    pipelineManager = _pipelineManager;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _dispatchCallback = std::move(dispatchCallback);
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

    downsampleResources = Render::ProceduralTextureGenerateResources(context);

    const uint32_t submitFamily = context->computeQueue != VK_NULL_HANDLE ? context->computeQueueFamily : context->graphicsQueueFamily;
    computeSubmit.Initialize(context, submitFamily);
}

void EnvironmentMapGenerateSlot::Launch(
    EnvironmentMapGenerateSlotHandle _slotHandle,
    const Core::Path& _imagePath,
    const Core::Path& _outputPath,
    Engine::EnvironmentMapID _environmentMapId,
    uint64_t _contentVersion)
{
    slotHandle = _slotHandle;
    imagePath = _imagePath;
    outputPath = _outputPath;
    environmentMapId = _environmentMapId;
    contentVersion = _contentVersion;
    bProbeMode = false;
    baseResolution = ENVIRONMENT_MAP_RESOLUTION;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }

    task.taskSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void EnvironmentMapGenerateSlot::LaunchProbe(
    EnvironmentMapGenerateSlotHandle _slotHandle,
    Core::HeapArray<uint16_t>* faces,
    uint32_t captureSize,
    uint32_t targetResolution,
    const Core::Path& _outputPath,
    Engine::EnvironmentMapID _environmentMapId,
    uint64_t _probeId,
    const Engine::ProbeBakeSnapshot& _snapshot,
    uint64_t _contentVersion)
{
    slotHandle = _slotHandle;
    imagePath = Core::Path{};
    outputPath = _outputPath;
    environmentMapId = _environmentMapId;
    contentVersion = _contentVersion;
    bProbeMode = true;
    baseResolution = targetResolution;
    probeCaptureSize = captureSize;
    probeId = _probeId;
    probeSnapshot = _snapshot;
    for (uint32_t face = 0; face < 6; ++face) {
        probeFaces[face] = std::move(faces[face]);
    }

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
    probeSourceImage = {};
    equiImage = {};
    equiImageView = {};
    mipmappedCubemapImage = {};
    mipmappedCubemapImageView = {};
    finalCubemapImage = {};
    for (auto& imgView : finalCubemapMipViews) {
        imgView = {};
    }
    for (auto& face : probeFaces) {
        face = {};
    }
    bProbeMode = false;
    probeCaptureSize = 0;
    probeId = 0;
    probeSnapshot = {};
    baseResolution = ENVIRONMENT_MAP_RESOLUTION;
    mipData = {};
    imageStagingAllocator.Reset();
    imageReceivingAllocator.Reset();
}

void EnvironmentMapGenerateSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
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
        VK_CHECK(vkResetFences(taskSlot->context->device, 1, &fence));
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        if (restart) {
            VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
        }
    };

    bool loadRes = taskSlot->bProbeMode
                       ? taskSlot->AssembleProbeCubemapAndGenerate(cmd, startRecording, submitAndWait)
                       : taskSlot->LoadEquirectangularAndGenerate(cmd, startRecording, submitAndWait);
    if (!loadRes) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        taskSlot->computeSubmit.Reset(taskSlot->context);
        return;
    }

    const bool bWriteRes = taskSlot->bProbeMode ? taskSlot->WriteWProbeFile() : taskSlot->WriteWEnvMapFile();
    if (!bWriteRes) {
        taskSlot->_notifyCallback(false, taskSlot->slotHandle);
        taskSlot->computeSubmit.Reset(taskSlot->context);
        return;
    }

    taskSlot->_notifyCallback(true, taskSlot->slotHandle);

    taskSlot->computeSubmit.Reset(taskSlot->context);
}

bool EnvironmentMapGenerateSlot::AssembleProbeCubemapAndGenerate(VkCommandBuffer cmd, const Core::InlineFunction<void()>& startRecording, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("AssembleProbeCubemapAndGenerate");

    const uint32_t S = probeCaptureSize;
    const uint32_t R = baseResolution;
    if (S == 0) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Probe assembly received an empty capture");
        return false;
    }

    const size_t faceTexels = static_cast<size_t>(S) * S;
    const size_t faceBytes = faceTexels * 4 * sizeof(uint16_t);
    const size_t uploadBytes = faceBytes * 6;

    imageStagingAllocator.Reset();
    const size_t allocation = imageStagingAllocator.Allocate(uploadBytes);
    if (allocation == SIZE_MAX) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Probe faces too large for staging buffer");
        return false;
    }

    startRecording();

    uint16_t* stagingBase = reinterpret_cast<uint16_t*>(static_cast<char*>(imageStagingBuffer.allocationInfo.pMappedData) + allocation);
    for (uint32_t face = 0; face < 6; ++face) {
        if (!probeFaces[face].IsAllocated()) {
            SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Probe face {} missing", face);
            return false;
        }
        const uint16_t* src = probeFaces[face].Data();
        uint16_t* dst = stagingBase + faceTexels * 4 * face;
        // Every face orientation reduces to a vertical flip of the captured image
        const size_t rowElems = static_cast<size_t>(S) * 4;
        for (uint32_t y = 0; y < S; ++y) {
            memcpy(dst + y * rowElems, src + (S - 1 - y) * rowElems, rowElems * sizeof(uint16_t));
        }
    }

    VkImageMemoryBarrier2 barrier{};
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

    // Halving chain so every capture texel contributes to the downsample; a single S -> R blit would point-skip most of the supersampled capture
    uint32_t sourceMipLevels = 1;
    for (uint32_t dim = S; dim > 2 * R; dim /= 2) {
        ++sourceMipLevels;
    }

    VkImageCreateInfo sourceCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        {S, S, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    );
    sourceCreateInfo.arrayLayers = 6;
    sourceCreateInfo.mipLevels = sourceMipLevels;
    probeSourceImage = Render::AllocatedImage::CreateAllocatedImage(context, sourceCreateInfo);

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        probeSourceImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    for (uint32_t face = 0; face < 6; ++face) {
        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = allocation + faceBytes * face;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = face;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = {S, S, 1};
        vkCmdCopyBufferToImage(cmd, imageStagingBuffer.handle, probeSourceImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        probeSourceImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    const Render::PipelineEntry resizeEntry = pipelineManager->GetPipelineEntrySnapshot(SID("procedural_resize"));
    if (resizeEntry.pipeline == VK_NULL_HANDLE) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] procedural_resize pipeline not ready");
        return false;
    }

    const uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(R))) + 1;

    // Per-face-per-mip 2D storage views; source block first, cubemap block after.
    constexpr uint32_t MAX_SOURCE_MIPS = 8;
    constexpr uint32_t MAX_CUBEMAP_MIPS = 13;
    assert(sourceMipLevels <= MAX_SOURCE_MIPS && mipLevels <= MAX_CUBEMAP_MIPS);
    assert(6 * (sourceMipLevels + mipLevels) <= Render::ProceduralTextureGenerateResources::MAX_RW_TEXTURES);
    Core::Array<Render::ImageView, 6 * MAX_SOURCE_MIPS> sourceFaceViews{};
    Core::Array<Render::ImageView, 6 * MAX_CUBEMAP_MIPS> cubemapFaceViews{};
    const uint32_t cubemapBaseIndex = 6 * sourceMipLevels;
    auto sourceIndex = [&](uint32_t face, uint32_t mip) { return face * sourceMipLevels + mip; };
    auto cubemapIndex = [&](uint32_t face, uint32_t mip) { return cubemapBaseIndex + face * mipLevels + mip; };

    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t mip = 0; mip < sourceMipLevels; ++mip) {
            VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(probeSourceImage.handle, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
            viewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, face, 1);
            sourceFaceViews[sourceIndex(face, mip)] = Render::ImageView::CreateImageView(context, viewInfo);
            downsampleResources.SetRWTexture({nullptr, sourceFaceViews[sourceIndex(face, mip)].handle, VK_IMAGE_LAYOUT_GENERAL}, sourceIndex(face, mip));
        }
    }

    VkDescriptorBufferBindingInfoEXT downsampleBinding = downsampleResources.GetBindingInfo();
    uint32_t downsampleBindingIndex = 0;
    VkDeviceSize downsampleBindingOffset = 0;
    vkCmdBindDescriptorBuffersEXT(cmd, 1, &downsampleBinding);
    vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resizeEntry.layout, 0, 1, &downsampleBindingIndex, &downsampleBindingOffset);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resizeEntry.pipeline);

    auto dispatchResize = [&](uint32_t srcDescIndex, uint32_t dstDescIndex, uint32_t dstDim) {
        ProceduralMipDownsamplePushConstant resizePC{srcDescIndex, dstDescIndex};
        vkCmdPushConstants(cmd, resizeEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ProceduralMipDownsamplePushConstant), &resizePC);
        const uint32_t groups = (dstDim + PROCEDURAL_TEXTURE_DISPATCH_X - 1) / PROCEDURAL_TEXTURE_DISPATCH_X;
        vkCmdDispatch(cmd, groups, groups, 1);
    };

    uint32_t sourceDim = S;
    for (uint32_t mip = 1; mip < sourceMipLevels; ++mip) {
        const uint32_t nextDim = sourceDim / 2;

        barrier = Render::VkHelpers::ImageMemoryBarrier(
            probeSourceImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6),
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);

        for (uint32_t face = 0; face < 6; ++face) {
            dispatchResize(sourceIndex(face, mip - 1), sourceIndex(face, mip), nextDim);
        }

        barrier = Render::VkHelpers::ImageMemoryBarrier(
            probeSourceImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);

        sourceDim = nextDim;
    }

    const uint32_t sourceFinalMip = sourceMipLevels - 1;

    VkImageCreateInfo cubemapCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        {R, R, 1},
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
    bool success = resourceManager->environmentMapGenerateResources.SetCubemap({nullptr, mipmappedCubemapImageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, 0);
    assert(success);

    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(mipmappedCubemapImage.handle, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
            viewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, face, 1);
            cubemapFaceViews[face * mipLevels + mip] = Render::ImageView::CreateImageView(context, viewInfo);
            downsampleResources.SetRWTexture({nullptr, cubemapFaceViews[face * mipLevels + mip].handle, VK_IMAGE_LAYOUT_GENERAL}, cubemapIndex(face, mip));
        }
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        mipmappedCubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Final step of the chain: last halved mip (within 2x of R) -> cubemap mip 0 (bilinear, may be non-integer scale)
    for (uint32_t face = 0; face < 6; ++face) {
        dispatchResize(sourceIndex(face, sourceFinalMip), cubemapIndex(face, 0), R);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        mipmappedCubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Build the source mip chain (prefilter/convolve sample it by LOD)
    {
        uint32_t mipDim = R;
        for (uint32_t mip = 1; mip < mipLevels; mip++) {
            const uint32_t nextDim = mipDim / 2;

            barrier = Render::VkHelpers::ImageMemoryBarrier(
                mipmappedCubemapImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6),
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            vkCmdPipelineBarrier2(cmd, &depInfo);

            for (uint32_t face = 0; face < 6; face++) {
                dispatchResize(cubemapIndex(face, mip - 1), cubemapIndex(face, mip), nextDim);
            }

            barrier = Render::VkHelpers::ImageMemoryBarrier(
                mipmappedCubemapImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            vkCmdPipelineBarrier2(cmd, &depInfo);

            mipDim = nextDim;
        }
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        mipmappedCubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    return BuildFilteredMipsAndCopy(cmd, submitAndWait);
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

        const Render::PipelineEntry pipelineEntry = pipelineManager->GetPipelineEntrySnapshot(SID("ibl_equirect_to_cubemap"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.pipeline);
        vkCmdPushConstants(cmd, pipelineEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(eqPc), &eqPc);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);
        uint32_t dispatchX = (ENVIRONMENT_MAP_RESOLUTION + ENVIRONMENT_MAP_GENERATION_DISPATCH_X - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_X;
        uint32_t dispatchY = (ENVIRONMENT_MAP_RESOLUTION + ENVIRONMENT_MAP_GENERATION_DISPATCH_Y - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_Y;
        vkCmdDispatch(cmd, dispatchX, dispatchY, 6);
        submitAndWait(true);
    }

    Core::Array<Render::ImageView, 6 * 13> cubemapFaceViews{};

    // Cubemap mipmap generation
    {
        ZoneScopedN("GenerateCubemapMipmaps");

        const Render::PipelineEntry resizeEntry = pipelineManager->GetPipelineEntrySnapshot(SID("procedural_resize"));
        if (resizeEntry.pipeline == VK_NULL_HANDLE) {
            SPDLOG_ERROR("[EnvironmentMapGenerateSlot] procedural_resize pipeline not ready");
            return false;
        }

        constexpr uint32_t MAX_CUBEMAP_MIPS = 13;
        assert(mipLevels <= MAX_CUBEMAP_MIPS && 6 * mipLevels <= Render::ProceduralTextureGenerateResources::MAX_RW_TEXTURES);
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t mip = 0; mip < mipLevels; ++mip) {
                VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(mipmappedCubemapImage.handle, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
                viewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, face, 1);
                cubemapFaceViews[face * mipLevels + mip] = Render::ImageView::CreateImageView(context, viewInfo);
                downsampleResources.SetRWTexture({nullptr, cubemapFaceViews[face * mipLevels + mip].handle, VK_IMAGE_LAYOUT_GENERAL}, face * mipLevels + mip);
            }
        }

        VkDescriptorBufferBindingInfoEXT downsampleBinding = downsampleResources.GetBindingInfo();
        uint32_t downsampleBindingIndex = 0;
        VkDeviceSize downsampleBindingOffset = 0;
        vkCmdBindDescriptorBuffersEXT(cmd, 1, &downsampleBinding);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resizeEntry.layout, 0, 1, &downsampleBindingIndex, &downsampleBindingOffset);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resizeEntry.pipeline);

        uint32_t mipDim = ENVIRONMENT_MAP_RESOLUTION;

        for (uint32_t mip = 1; mip < mipLevels; mip++) {
            const uint32_t nextDim = mipDim / 2;

            barrier = Render::VkHelpers::ImageMemoryBarrier(
                mipmappedCubemapImage.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 6),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            vkCmdPipelineBarrier2(cmd, &depInfo);

            for (uint32_t face = 0; face < 6; face++) {
                ProceduralMipDownsamplePushConstant resizePC{face * mipLevels + mip - 1, face * mipLevels + mip};
                vkCmdPushConstants(cmd, resizeEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ProceduralMipDownsamplePushConstant), &resizePC);
                const uint32_t groups = (nextDim + PROCEDURAL_TEXTURE_DISPATCH_X - 1) / PROCEDURAL_TEXTURE_DISPATCH_X;
                vkCmdDispatch(cmd, groups, groups, 1);
            }

            mipDim = nextDim;
        }

        barrier = Render::VkHelpers::ImageMemoryBarrier(
            mipmappedCubemapImage.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    return BuildFilteredMipsAndCopy(cmd, submitAndWait);
}

bool EnvironmentMapGenerateSlot::BuildFilteredMipsAndCopy(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("BuildFilteredMipsAndCopy");

    const uint32_t diffuseResolution = baseResolution >> ENVIRONMENT_MAP_DIFFUSE_MIP;

    VkImageMemoryBarrier2 barrier{};
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

    VkImageCreateInfo finalCubemapCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        {baseResolution, baseResolution, 1},
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
        bool success = resourceManager->environmentMapGenerateResources.SetRWCubemapHalfArray(
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

    // Generate specular prefilter for mips 0 to ENVIRONMENT_MAP_DIFFUSE_MIP-1
    {
        ZoneScopedN("PrefilterSpecular");
        const Render::PipelineEntry pipelineEntry = pipelineManager->GetPipelineEntrySnapshot(SID("ibl_prefilter_specular"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.pipeline);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);

        for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_DIFFUSE_MIP; mip++) {
            // Squared spacing concentrates mip fidelity at low roughness; the lookup inverts with sqrt
            const float mipFraction = static_cast<float>(mip) / static_cast<float>(ENVIRONMENT_MAP_DIFFUSE_MIP - 1);
            float roughness = mipFraction * mipFraction;
            uint32_t mipResolution = baseResolution >> mip;

            PrefilterSpecularPushConstant pc{
                .samplerIndex = CUBEMAP_IMAGE_SAMPLER_INDEX,
                .sourceIndex = 0, // mipmapped cubemap index (0, float4 sampler descriptor)
                .targetIndex = mip, // final cubemap index (half4 storage descriptor)
                .roughness = roughness,
                .width = mipResolution,
                .height = mipResolution,
                .sampleCount = 4096,
                .fireflyThreshold = 10.0f,
                .sourceResolution = baseResolution
            };

            vkCmdPushConstants(cmd, pipelineEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t dispatchX = (mipResolution + ENVIRONMENT_MAP_GENERATION_DISPATCH_X - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_X;
            uint32_t dispatchY = (mipResolution + ENVIRONMENT_MAP_GENERATION_DISPATCH_Y - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_Y;
            vkCmdDispatch(cmd, dispatchX, dispatchY, 6);
        }
    }

    // Generate diffuse irradiance for the last mip
    {
        ZoneScopedN("ConvolveDiffuse");
        const Render::PipelineEntry pipelineEntry = pipelineManager->GetPipelineEntrySnapshot(SID("ibl_convolve_diffuse"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.pipeline);
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);

        ConvolveDiffusePushConstant pc{
            .samplerIndex = CUBEMAP_IMAGE_SAMPLER_INDEX,
            .sourceIndex = 0, // mipmapped cubemap index
            .targetIndex = ENVIRONMENT_MAP_DIFFUSE_MIP,
            .targetWidth = diffuseResolution,
            .targetHeight = diffuseResolution,
            .sampleDelta = 0.025f
        };

        vkCmdPushConstants(cmd, pipelineEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t dispatchX = (diffuseResolution + ENVIRONMENT_MAP_GENERATION_DISPATCH_X - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_X;
        uint32_t dispatchY = (diffuseResolution + ENVIRONMENT_MAP_GENERATION_DISPATCH_Y - 1) / ENVIRONMENT_MAP_GENERATION_DISPATCH_Y;
        vkCmdDispatch(cmd, dispatchX, dispatchY, 6);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        finalCubemapImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, ENVIRONMENT_MAP_MIPS, 0, 6),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Copy all mip faces back to CPU for blob serialization
    {
        ZoneScopedN("CopyCubemapToCPU");

        for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_MIPS; mip++) {
            uint32_t mipResolution = (mip < ENVIRONMENT_MAP_DIFFUSE_MIP) ? (baseResolution >> mip) : diffuseResolution;
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

    const Engine::WImageDesc desc{VK_FORMAT_R16G16B16A16_SFLOAT, baseResolution, baseResolution, ENVIRONMENT_MAP_MIPS, 6};
    const size_t blobSize = Engine::WImageBlobSize(desc);
    auto blob = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, blobSize);
    if (Engine::WImageBlobInit(blob.Data(), blob.Size(), desc) == 0) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to lay out environment map image blob");
        return false;
    }
    for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_MIPS; mip++) {
        for (uint32_t face = 0; face < 6; face++) {
            memcpy(Engine::WImageFaceData(blob.Data(), mip, face), mipData[mip][face].Data(), mipData[mip][face].Size());
        }
    }

    auto maxCompressedSize = Engine::CompressMaxSize(Engine::DEFAULT_ENV_MAP_COMPRESSION, blobSize);
    auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
    size_t realCompressedSize = Engine::Compress(Engine::DEFAULT_ENV_MAP_COMPRESSION, blob.Data(), blobSize, compressed.Data(), compressed.Size());

    Engine::WEnvMapHeader header{};
    header.environmentMapId = environmentMapId.id;
    header.contentVersion = contentVersion;
    header.width = baseResolution;
    header.height = baseResolution;
    header.mipCount = ENVIRONMENT_MAP_MIPS;
    header.uncompressedSize = blobSize;
    header.dataSize = realCompressedSize;

    Core::InlineString stem{imagePath.IsEmpty() ? Core::InlineString(outputPath.Stem()) : Core::InlineString(imagePath.Stem())};
    const size_t copyLen = std::min(stem.Size(), Engine::WENVMAP_NAME_LENGTH - 1);
    memcpy(header.name, stem.c_str(), copyLen);
    header.name[copyLen] = '\0';

    Core::Vector<std::byte> headerOut(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Engine::WriteWEnvMapHeader(headerOut, header);
    if (!Platform::WriteFile(outputPath, headerOut.Data(), headerOut.Size()) ||
        !Platform::AppendFile(outputPath, compressed.Data(), realCompressedSize)) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to write output file: {}", outputPath.c_str());
        return false;
    }

    SPDLOG_INFO("[EnvironmentMapGenerateSlot] Wrote {}", outputPath.c_str());
    return true;
}

bool EnvironmentMapGenerateSlot::WriteWProbeFile()
{
    ZoneScopedN("WriteWProbeFile");

    const Engine::WImageDesc desc{VK_FORMAT_R16G16B16A16_SFLOAT, baseResolution, baseResolution, ENVIRONMENT_MAP_MIPS, 6};
    const size_t blobSize = Engine::WImageBlobSize(desc);
    auto blob = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, blobSize);
    if (Engine::WImageBlobInit(blob.Data(), blob.Size(), desc) == 0) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to lay out probe image blob");
        return false;
    }
    for (uint32_t mip = 0; mip < ENVIRONMENT_MAP_MIPS; mip++) {
        for (uint32_t face = 0; face < 6; face++) {
            memcpy(Engine::WImageFaceData(blob.Data(), mip, face), mipData[mip][face].Data(), mipData[mip][face].Size());
        }
    }

    auto maxCompressedSize = Engine::CompressMaxSize(Engine::DEFAULT_ENV_MAP_COMPRESSION, blobSize);
    auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
    size_t realCompressedSize = Engine::Compress(Engine::DEFAULT_ENV_MAP_COMPRESSION, blob.Data(), blobSize, compressed.Data(), compressed.Size());

    Engine::WProbeHeader header{};
    header.probeId = probeId;
    header.environmentMapId = environmentMapId.id;
    header.contentVersion = contentVersion;
    header.width = baseResolution;
    header.height = baseResolution;
    header.mipCount = ENVIRONMENT_MAP_MIPS;
    header.uncompressedSize = blobSize;
    header.dataSize = realCompressedSize;
    header.resolution = baseResolution;
    header.snapshot = probeSnapshot;

    Core::InlineString stem{Core::InlineString(outputPath.Stem())};
    const size_t copyLen = std::min(stem.Size(), Engine::WENVMAP_NAME_LENGTH - 1);
    memcpy(header.name, stem.c_str(), copyLen);
    header.name[copyLen] = '\0';

    Core::Vector<std::byte> headerOut(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Engine::WriteWProbeHeader(headerOut, header);
    if (!Platform::WriteFile(outputPath, headerOut.Data(), headerOut.Size()) ||
        !Platform::AppendFile(outputPath, compressed.Data(), realCompressedSize)) {
        SPDLOG_ERROR("[EnvironmentMapGenerateSlot] Failed to write output file: {}", outputPath.c_str());
        return false;
    }

    SPDLOG_INFO("[EnvironmentMapGenerateSlot] Wrote {}", outputPath.c_str());
    return true;
}
} // namespace Editor
