//
// Created by William on 2026-02-16.
//

#include "cubemap_load_slot.h"
#include "wimage_layer_upload.h"

#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "core/containers/heap_array.h"
#include "core/memory/memory_manager.h"
#include "engine/compression/compression.h"
#include "engine/resources/environment_map/environment_map_format.h"
#include "platform/file_utils.h"
#include "render/resource_manager.h"
#include "render/types/cubemap_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
CubemapLoadSlot::CubemapLoadSlot() = default;

CubemapLoadSlot::~CubemapLoadSlot()
{
    transferSubmit.Destroy(context);
}

void CubemapLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, CubemapSlotHandle cubemapSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    transferSubmit.Initialize(context, context->transferQueueFamily);
}

void CubemapLoadSlot::Launch(
    CubemapSlotHandle _cubemapSlotHandle,
    UploadStaging* _uploadStaging,
    Render::Cubemap* _outputCubemap)
{
    cubemapSlotHandle = _cubemapSlotHandle;
    uploadStaging = _uploadStaging;
    outputCubemap = _outputCubemap;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void CubemapLoadSlot::Clear()
{
    cubemapSlotHandle = {};
    outputCubemap = nullptr;
    uploadStaging = nullptr;

    blobData = {};
    blobView = {};
}

void CubemapLoadSlot::LoadCubemapTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadCubemapFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->cubemapSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->cubemapSlotHandle);
        loadSlot->Clear();
        return;
    }

    VkCommandBuffer cmd = loadSlot->transferSubmit.cmd;
    VkFence fence = loadSlot->transferSubmit.fence;

    auto submitAndWait = [&](bool reset) {
        ZoneScopedN("SubmitAndWait");

        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_requestDispatchCallback(cmd, fence, &done);
        done.acquire();
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

    loadSlot->UploadCubemap(cmd, submitAndWait);

    VK_CHECK(vkEndCommandBuffer(cmd));
    std::binary_semaphore done(0);
    loadSlot->_requestDispatchCallback(cmd, fence, &done);
    done.acquire();
    VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

    loadSlot->PostUploadSetup();

    loadSlot->transferSubmit.Reset(loadSlot->context);

    loadSlot->_notifyCallback(true, loadSlot->cubemapSlotHandle);
}

bool CubemapLoadSlot::LoadCubemapFromDisk()
{
    ZoneScopedN("LoadCubemapFromDisk");

    if (!outputCubemap) {
        SPDLOG_ERROR("Output cubemap is null");
        return false;
    }

    const Core::Path& cubemapPath = outputCubemap->source;

    {
        ZoneScopedN("FileExistsCheck");
        if (!Platform::FileExists(cubemapPath)) {
            SPDLOG_ERROR("Failed to find cubemap: {}", cubemapPath.c_str());
            return false;
        }
    }

    {
        ZoneScopedN("WImageLoad");

        Platform::ScopedFileMapping map(cubemapPath, true);
        if (!map.data || outputCubemap->dataOffset + outputCubemap->dataSize > map.size) {
            SPDLOG_ERROR("Failed to read .wenvmap data: {}", cubemapPath.c_str());
            return false;
        }

        blobData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetTexture, outputCubemap->uncompressedSize);
        {
            ZoneScopedN("Decompress");
            Engine::Decompress(outputCubemap->compressionType, map.data + outputCubemap->dataOffset, outputCubemap->dataSize, blobData.Data(), outputCubemap->uncompressedSize);
        }
        {
            ZoneScopedN("WImageParse");
            if (!blobView.Parse(blobData.Data(), blobData.Size())) {
                SPDLOG_ERROR("Failed to parse cubemap image blob: {}", cubemapPath.c_str());
                return false;
            }
        }
    }

    if (!blobView.bCubemap) {
        SPDLOG_ERROR("Expected cubemap texture: {}", cubemapPath.c_str());
        return false;
    }

    if (blobView.RowPitch(0) > uploadStaging->GetStagingAllocator().GetCapacity()) {
        SPDLOG_ERROR("Cubemap block row too large for staging buffer: {}", cubemapPath.c_str());
        return false;
    }

    return true;
}

bool CubemapLoadSlot::AllocateGPUResources()
{
    VkExtent3D extent{
        .width = blobView.baseWidth,
        .height = blobView.baseHeight,
        .depth = 1
    };

    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        blobView.vkFormat,
        extent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = blobView.levelCount;
    imageCreateInfo.arrayLayers = 6;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    outputCubemap->image = Render::AllocatedImage::CreateAllocatedImage(context, context->ApplyImageSharing(imageCreateInfo, true));

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        outputCubemap->image.handle,
        outputCubemap->image.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.subresourceRange.layerCount = 6;
    viewInfo.subresourceRange.levelCount = blobView.levelCount;

    outputCubemap->imageView = Render::ImageView::CreateImageView(context, viewInfo);

    return true;
}

void CubemapLoadSlot::UploadCubemap(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadCubemap");
    UploadWImageLayers(cmd, blobView, outputCubemap->image.handle, uploadStaging, submitAndWait);
    blobData = {};
    blobView = {};
}

void CubemapLoadSlot::PostUploadSetup()
{
    bool updateRes = resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateCubemap(
        outputCubemap->bindlessHandle, {
            .imageView = outputCubemap->imageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });

    if (!updateRes) {
        SPDLOG_ERROR("Failed to update bindless cubemap descriptor");
    }
}
} // AssetLoad