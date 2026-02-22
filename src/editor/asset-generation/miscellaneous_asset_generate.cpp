//
// Created by William on 2026-02-17.
//

#include "miscellaneous_asset_generate.h"

#include <ktx.h>
#include <semaphore>

#include "render/resource_manager.h"
#include "render/descriptors/vk_bindless_resources_storage.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_utils.h"

namespace Editor
{
void CreateBRDFLookupTable(
    std::filesystem::path outputPath,
    Render::VulkanContext* context,
    Render::ResourceManager* resourceManager,
    Render::PipelineManager* pipelineManager,
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback)
{
    VkCommandPoolCreateInfo graphicsPoolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->graphicsQueueFamily);
    VkCommandPool graphicsCommandPool;
    VK_CHECK(vkCreateCommandPool(context->device, &graphicsPoolInfo, nullptr, &graphicsCommandPool));

    VkCommandBufferAllocateInfo graphicsCmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, graphicsCommandPool);
    VkCommandBuffer graphicsCmd;
    VK_CHECK(vkAllocateCommandBuffers(context->device, &graphicsCmdInfo, &graphicsCmd));

    VkFenceCreateInfo graphicsFenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence graphicsFence;
    VK_CHECK(vkCreateFence(context->device, &graphicsFenceInfo, nullptr, &graphicsFence));

    auto startGraphicsRecording = [&] {
        VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &beginInfo));
    };

    auto graphicsSubmitAndWait = [&](bool restart) {
        ZoneScopedN("GraphicsSubmitAndWait");
        VK_CHECK(vkEndCommandBuffer(graphicsCmd));
        std::binary_semaphore done(0);
        graphicsDispatchCallback(graphicsCmd, graphicsFence, &done);
        done.acquire();
        VK_CHECK(vkResetFences(context->device, 1, &graphicsFence));
        VK_CHECK(vkResetCommandBuffer(graphicsCmd, 0));

        if (restart) {
            VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(graphicsCmd, &beginInfo));
        }
    };

    // Image Creation
    constexpr uint32_t LUT_SIZE = 512;
    VkImageCreateInfo lutImageInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R16G16_SFLOAT,
        {LUT_SIZE, LUT_SIZE, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    );
    Render::AllocatedImage lutImage = Render::AllocatedImage::CreateAllocatedImage(context, lutImageInfo);

    VkImageViewCreateInfo lutViewInfo = Render::VkHelpers::ImageViewCreateInfo(
        lutImage.handle,
        VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    lutViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    Render::ImageView lutImageView = Render::ImageView::CreateImageView(context, lutViewInfo);
    bool success = resourceManager->brdfLutGenerateResources.WriteDescriptor(0, {nullptr, lutImageView.handle, VK_IMAGE_LAYOUT_GENERAL});
    assert(success);

    startGraphicsRecording();

    VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
        lutImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(graphicsCmd, &depInfo);

    //
    {
        BRDFLUTPushConstant pc{
            .targetIndex = 0
        };

        std::array bindings{resourceManager->brdfLutGenerateResources.GetBindingInfo()};
        uint32_t bindingIndex{0u};
        VkDeviceSize bindingOffset{0};
        vkCmdBindDescriptorBuffersEXT(graphicsCmd, bindings.size(), bindings.data());

        const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ibl_brdf_lut"));
        if (!pipelineEntry) {
            SPDLOG_ERROR("[CreateBRDFLookupTable] \"ibl_brdf_lut\" Pipeline doesn't exist");
            return;
        }
        vkCmdBindPipeline(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(graphicsCmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdSetDescriptorBufferOffsetsEXT(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.size(), &bindingIndex, &bindingOffset);
        vkCmdDispatch(graphicsCmd,
                      (LUT_SIZE + BRDF_LUT_GENERATION_DISPATCH_X - 1) / BRDF_LUT_GENERATION_DISPATCH_X,
                      (LUT_SIZE + BRDF_LUT_GENERATION_DISPATCH_Y - 1) / BRDF_LUT_GENERATION_DISPATCH_Y,
                      1);
        graphicsSubmitAndWait(true);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        lutImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    depInfo = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(graphicsCmd, &depInfo);


    // Copy back to CPU for KTX generation
    constexpr VkDeviceSize lutByteSize = LUT_SIZE * LUT_SIZE * sizeof(uint16_t) * 2;
    Render::AllocatedBuffer stagingBuffer = Render::AllocatedBuffer::CreateAllocatedReceivingBuffer(context, lutByteSize);
    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = {LUT_SIZE, LUT_SIZE, 1};
    vkCmdCopyImageToBuffer(graphicsCmd, lutImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer.handle, 1, &copyRegion);
    graphicsSubmitAndWait(false);
    std::vector<uint8_t> lutData(lutByteSize);
    memcpy(lutData.data(), stagingBuffer.allocationInfo.pMappedData, lutByteSize);


    // Write to KTX2
    ktxTexture2* texture;
    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R16G16_SFLOAT;
    createInfo.baseWidth = LUT_SIZE;
    createInfo.baseHeight = LUT_SIZE;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = 1;
    createInfo.numLayers = 1;
    createInfo.numFaces = 1;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[CreateBRDFLookupTable] Failed to create KTX texture");
        return;
    }

    ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, lutData.data(), lutData.size());
    result = ktxTexture_WriteToNamedFile(ktxTexture(texture), outputPath.string().c_str());
    ktxTexture_Destroy(ktxTexture(texture));
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("[CreateBRDFLookupTable] Failed to write KTX file: {}", outputPath.string());
        return;
    }
    SPDLOG_INFO("[CreateBRDFLookupTable] Wrote {}", outputPath.string());

    vkDestroyFence(context->device, graphicsFence, nullptr);
    vkDestroyCommandPool(context->device, graphicsCommandPool, nullptr);
}
} // Editor
