//
// Created by William on 2026-10-08.
//

#include "texture_array_load_slot.h"
#include "wimage_layer_upload.h"

#include "core/memory/memory_manager.h"
#include "engine/compression/compression.h"
#include "platform/file_utils.h"
#include "render/resource_manager.h"
#include "render/types/texture_array_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
TextureArrayLoadSlot::TextureArrayLoadSlot() = default;

TextureArrayLoadSlot::~TextureArrayLoadSlot()
{
    transferSubmit.Destroy(context);
}

void TextureArrayLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, TextureArraySlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    transferSubmit.Initialize(context, context->transferQueueFamily);
}

void TextureArrayLoadSlot::Launch(TextureArraySlotHandle _slotHandle, UploadStaging* _uploadStaging, Render::TextureArray* _outputTextureArray)
{
    slotHandle = _slotHandle;
    uploadStaging = _uploadStaging;
    outputTextureArray = _outputTextureArray;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void TextureArrayLoadSlot::Clear()
{
    slotHandle = {};
    outputTextureArray = nullptr;
    uploadStaging = nullptr;

    blobData = {};
    blobView = {};
}

void TextureArrayLoadSlot::LoadTextureArrayTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->slotHandle);
        loadSlot->Clear();
        return;
    }

    loadSlot->AllocateGPUResources();

    VkCommandBuffer cmd = loadSlot->transferSubmit.cmd;
    VkFence fence = loadSlot->transferSubmit.fence;

    auto submit = [&]() {
        ZoneScopedN("SubmitAndWait");
        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_requestDispatchCallback(cmd, fence, &done);
        done.acquire();
        VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));
    };
    auto submitAndRestart = [&](bool) {
        submit();
        VK_CHECK(vkResetFences(loadSlot->context->device, 1, &fence));
        VK_CHECK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    };

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    UploadWImageLayers(cmd, loadSlot->blobView, loadSlot->outputTextureArray->image.handle, loadSlot->uploadStaging, submitAndRestart);
    submit();

    loadSlot->blobData = {};
    loadSlot->blobView = {};

    const bool bUpdated = loadSlot->resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateTextureArray(
        loadSlot->outputTextureArray->bindlessHandle, {
            .imageView = loadSlot->outputTextureArray->imageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
    if (!bUpdated) {
        SPDLOG_ERROR("Failed to update bindless texture array descriptor");
    }

    loadSlot->transferSubmit.Reset(loadSlot->context);

    loadSlot->_notifyCallback(true, loadSlot->slotHandle);
}

bool TextureArrayLoadSlot::LoadFromDisk()
{
    ZoneScopedN("TextureArrayLoadFromDisk");

    const Core::Path& path = outputTextureArray->source;
    Platform::ScopedFileMapping map(path, true);
    if (!map.data || outputTextureArray->dataOffset + outputTextureArray->dataSize > map.size) {
        SPDLOG_ERROR("Failed to read texture array data: {}", path.c_str());
        return false;
    }

    blobData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetTexture, outputTextureArray->uncompressedSize);
    Engine::Decompress(outputTextureArray->compressionType, map.data + outputTextureArray->dataOffset, outputTextureArray->dataSize, blobData.Data(), outputTextureArray->uncompressedSize);
    if (!blobView.Parse(blobData.Data(), blobData.Size())) {
        SPDLOG_ERROR("Failed to parse texture array image blob: {}", path.c_str());
        return false;
    }

    if (blobView.RowPitch(0) > uploadStaging->GetStagingAllocator().GetCapacity()) {
        SPDLOG_ERROR("Texture array block row too large for staging buffer: {}", path.c_str());
        return false;
    }
    return true;
}

void TextureArrayLoadSlot::AllocateGPUResources()
{
    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        blobView.vkFormat,
        {blobView.baseWidth, blobView.baseHeight, 1},
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.mipLevels = blobView.levelCount;
    imageCreateInfo.arrayLayers = blobView.faceCount;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    outputTextureArray->image = Render::AllocatedImage::CreateAllocatedImage(context, context->ApplyImageSharing(imageCreateInfo, true));

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(outputTextureArray->image.handle, outputTextureArray->image.format, VK_IMAGE_ASPECT_COLOR_BIT);
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.subresourceRange.layerCount = blobView.faceCount;
    viewInfo.subresourceRange.levelCount = blobView.levelCount;
    outputTextureArray->imageView = Render::ImageView::CreateImageView(context, viewInfo);
}
} // AssetLoad
