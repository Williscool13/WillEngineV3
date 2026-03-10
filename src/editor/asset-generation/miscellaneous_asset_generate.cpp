//
// Created by William on 2026-02-17.
//

#include "miscellaneous_asset_generate.h"

#include <cstring>
#include <fstream>
#include <ktx.h>
#include <semaphore>
#include <tracy/Tracy.hpp>

#include "core/hash/fnv_1_a.h"
#include "engine/logging/engine_log.h"
#include "engine/textures/texture_format.h"
#include "platform/paths.h"
#include "render/resource_manager.h"
#include "render/descriptors/vk_bindless_resources_storage.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_utils.h"

namespace Editor
{
bool WriteSimpleRGBA8WTexture(const std::filesystem::path& outputPath, Engine::TextureID id, const char* name, uint32_t w, uint32_t h, const uint8_t* rgba)
{
    ktxTexture2* texture;
    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat        = VK_FORMAT_R8G8B8A8_SRGB;
    createInfo.baseWidth       = w;
    createInfo.baseHeight      = h;
    createInfo.baseDepth       = 1;
    createInfo.numDimensions   = 2;
    createInfo.numLevels       = 1;
    createInfo.numLayers       = 1;
    createInfo.numFaces        = 1;
    createInfo.isArray         = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to create KTX texture for {}", name);
        return false;
    }

    ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, rgba, w * h * 4);

    constexpr char writer[] = "WillEngine";
    ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

    ktx_uint8_t* ktxBytes{nullptr};
    ktx_size_t   ktxSize{0};
    result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
    ktxTexture_Destroy(ktxTexture(texture));
    if (result != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to serialise {} to memory", name);
        return false;
    }

    Engine::WTextureHeader header{};
    header.textureId = id.id;
    header.width     = w;
    header.height    = h;
    header.mipCount  = 1;
    header.dataSize  = static_cast<uint64_t>(ktxSize);
    strncpy_s(header.name, name, Engine::WTEXTURE_NAME_LENGTH - 1);

    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream f(outputPath, std::ios::binary);
    if (!f || !Engine::WriteWTextureHeader(f, header)) {
        free(ktxBytes);
        LOG_ERROR(Asset, "Failed to write .wtexture: {}", outputPath.string());
        return false;
    }
    f.write(reinterpret_cast<const char*>(ktxBytes), static_cast<std::streamsize>(ktxSize));
    free(ktxBytes);

    LOG_INFO(Asset, "Wrote {}", outputPath.string());
    return true;
}

void CreateCriticalEngineResources()
{
    const std::filesystem::path texturesPath = Platform::GetAssetPath() / "textures";

    std::filesystem::path whitePath = texturesPath / "white.wtexture";
    if (!exists(whitePath)) {
        // White: 1x1 opaque white
        {
            constexpr uint8_t pixels[4] = {255, 255, 255, 255};
            WriteSimpleRGBA8WTexture(
                whitePath,
                Engine::TextureID(fnv1a64("white", 5)),
                "white", 1, 1, pixels
            );
        }
    }

    std::filesystem::path errorPath = texturesPath / "error.wtexture";
    if (!exists(errorPath)) {
        // Error: 4x4 alternating magenta/black checkerboard
        constexpr uint8_t magenta[4] = {255, 0, 255, 255};
        constexpr uint8_t black[4]   = {0,   0, 0,   255};
        uint8_t pixels[4 * 4 * 4];
        for (uint32_t y = 0; y < 4; ++y) {
            for (uint32_t x = 0; x < 4; ++x) {
                memcpy(&pixels[(y * 4 + x) * 4], ((x + y) % 2 == 0) ? magenta : black, 4);
            }
        }
        WriteSimpleRGBA8WTexture(
            errorPath,
            Engine::TextureID(fnv1a64("error", 5)),
            "error", 4, 4, pixels
        );
    }
}

void CreateBRDFLookupTable(
    std::filesystem::path outputPath,
    Engine::TextureID textureId,
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
            LOG_ERROR(Asset, "\"ibl_brdf_lut\" pipeline doesn't exist");
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
        LOG_ERROR(Asset, "Failed to create KTX texture for BRDF LUT");
        return;
    }

    ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, lutData.data(), lutData.size());

    const char writer[] = "WillEngine";
    ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

    ktx_uint8_t* ktxBytes{nullptr};
    ktx_size_t   ktxSize{0};
    result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
    ktxTexture_Destroy(ktxTexture(texture));
    if (result != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to serialise BRDF LUT to memory");
        return;
    }

    Engine::WTextureHeader header{};
    header.textureId = textureId.id;
    header.width     = LUT_SIZE;
    header.height    = LUT_SIZE;
    header.mipCount  = 1;
    header.dataSize  = static_cast<uint64_t>(ktxSize);
    strncpy_s(header.name, outputPath.stem().string().c_str(), Engine::WTEXTURE_NAME_LENGTH - 1);

    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream f(outputPath, std::ios::binary);
    if (!f || !Engine::WriteWTextureHeader(f, header)) {
        free(ktxBytes);
        LOG_ERROR(Asset, "Failed to write .wtexture: {}", outputPath.string());
        return;
    }
    f.write(reinterpret_cast<const char*>(ktxBytes), static_cast<std::streamsize>(ktxSize));
    free(ktxBytes);

    LOG_INFO(Asset, "Wrote {}", outputPath.string());

    vkDestroyFence(context->device, graphicsFence, nullptr);
    vkDestroyCommandPool(context->device, graphicsCommandPool, nullptr);
}
} // Editor
