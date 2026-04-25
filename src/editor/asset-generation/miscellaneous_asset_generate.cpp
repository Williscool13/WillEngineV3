//
// Created by William on 2026-02-17.
//

#include "miscellaneous_asset_generate.h"

#include <cstring>
#include <fstream>
#include <ktx.h>
#include <semaphore>
#include <tracy/Tracy.hpp>

#include "core/containers/function.h"
#include "core/containers/heap_array.h"
#include "core/containers/inline_function.h"
#include "core/hash/fnv_1_a.h"
#include "core/memory/memory_manager.h"
#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/texture/texture_format.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/resource_manager.h"
#include "render/descriptors/vk_bindless_resources_storage.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_utils.h"

namespace Editor
{
bool WriteSimpleRGBA8WTexture(Core::MemoryManager* memoryManager, const char* outputPath, Engine::TextureID id, const char* name, uint32_t w, uint32_t h, const uint8_t* rgba)
{
    ktxTexture2* texture;
    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R8G8B8A8_SRGB;
    createInfo.baseWidth = w;
    createInfo.baseHeight = h;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = 1;
    createInfo.numLayers = 1;
    createInfo.numFaces = 1;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (result != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to create KTX texture for {}", name);
        return false;
    }

    ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, rgba, w * h * 4);

    constexpr char writer[] = "WillEngine";
    ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

    // todo: remove ktxlib from this whole thing is possible
    ktx_uint8_t* ktxBytes{nullptr};
    ktx_size_t ktxSize{0};
    result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
    ktxTexture_Destroy(ktxTexture(texture));
    if (result != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to serialise {} to memory", name);
        return false;
    }

    auto maxCompressedSize = Engine::CompressLZ4MaxSize(ktxSize);
    auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
    size_t realSize = Engine::CompressLZ4(ktxBytes, ktxSize, compressed.Data(), compressed.Size());

    free(ktxBytes);

    Engine::WTextureHeader header{};
    header.textureId = id.id;
    header.width = w;
    header.height = h;
    header.mipCount = 1;
    header.uncompressedSize = ktxSize;
    header.dataSize = realSize;
    strncpy_s(header.name, name, Engine::WTEXTURE_NAME_LENGTH - 1);

    Platform::CreateDirectories(Core::Path(outputPath).Parent().c_str());
    std::ofstream f(outputPath, std::ios::binary);
    if (!f || !Engine::WriteWTextureHeader(f, header)) {
        LOG_ERROR(Asset, "Failed to write .wtexture: {}", outputPath);
        return false;
    }
    f.write(reinterpret_cast<const char*>(compressed.Data()), static_cast<std::streamsize>(realSize));

    LOG_INFO(Asset, "Wrote {}", outputPath);
    return true;
}

void CreateCriticalEngineResources(Core::MemoryManager* memoryManager)
{
    const Core::Path texturesPath = Platform::GetAssetPath() / "textures";

    const Core::Path whitePath = texturesPath / "white.wtexture";
    if (!whitePath.Exists()) {
        constexpr uint8_t pixels[4] = {255, 255, 255, 255};
        WriteSimpleRGBA8WTexture(
            memoryManager,
            whitePath.c_str(),
            Engine::TextureID(fnv1a64("white", 5)), "engine_default_white", 1, 1, pixels
        );
    }

    const Core::Path errorPath = texturesPath / "error.wtexture";
    if (!errorPath.Exists()) {
        // 4x4 alternating magenta/black checkerboard
        constexpr uint8_t magenta[4] = {255, 0, 255, 255};
        constexpr uint8_t black[4] = {0, 0, 0, 255};
        uint8_t pixels[4 * 4 * 4];
        for (uint32_t y = 0; y < 4; ++y) {
            for (uint32_t x = 0; x < 4; ++x) {
                memcpy(&pixels[(y * 4 + x) * 4], ((x + y) % 2 == 0) ? magenta : black, 4);
            }
        }
        WriteSimpleRGBA8WTexture(
            memoryManager,
            errorPath.c_str(),
            Engine::TextureID(fnv1a64("error", 5)), "engine_default_error", 4, 4, pixels
        );
    }
}

void CreateBRDFLookupTable(
    Core::MemoryManager* memoryManager,
    Core::Path outputPath,
    Engine::TextureID textureId,
    Render::VulkanContext* context,
    Render::ResourceManager* resourceManager,
    Render::PipelineManager* pipelineManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback)
{
    if (outputPath.Exists()) {
        LOG_INFO(Asset, "Skipping BRDF LUT generation, file already exists: {}", outputPath.c_str());
        return;
    }

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

        Core::Array<VkDescriptorBufferBindingInfoEXT, 1> bindings{resourceManager->brdfLutGenerateResources.GetBindingInfo()};
        uint32_t bindingIndex{0u};
        VkDeviceSize bindingOffset{0};
        vkCmdBindDescriptorBuffersEXT(graphicsCmd, bindings.Size(), bindings.Data());

        const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ibl_brdf_lut"));
        if (!pipelineEntry) {
            LOG_ERROR(Asset, "\"ibl_brdf_lut\" pipeline doesn't exist");
            return;
        }
        vkCmdBindPipeline(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(graphicsCmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdSetDescriptorBufferOffsetsEXT(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);
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

    auto lutData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, lutByteSize);
    memcpy(lutData.Data(), stagingBuffer.allocationInfo.pMappedData, lutByteSize);


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

    ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, lutData.Data(), lutData.Size());

    const char writer[] = "WillEngine";
    ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

    ktx_uint8_t* ktxBytes{nullptr};
    ktx_size_t ktxSize{0};
    result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
    ktxTexture_Destroy(ktxTexture(texture));
    if (result != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to serialise BRDF LUT to memory");
        return;
    }

    auto maxCompressedSize = Engine::CompressLZ4MaxSize(ktxSize);
    auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
    size_t realSize = Engine::CompressLZ4(ktxBytes, ktxSize, compressed.Data(), compressed.Size());

    free(ktxBytes);

    Engine::WTextureHeader header{};
    header.textureId = textureId.id;
    header.width = LUT_SIZE;
    header.height = LUT_SIZE;
    header.mipCount = 1;
    header.uncompressedSize = ktxSize;
    header.dataSize = realSize;

    strncpy_s(header.name, Engine::WTEXTURE_NAME_LENGTH, "brdf_lut", Engine::WTEXTURE_NAME_LENGTH - 1);

    Core::Path outputParent = outputPath.Parent();
    Platform::CreateDirectories(outputParent.c_str());
    std::ofstream f(outputPath.c_str(), std::ios::binary);
    if (!f || !Engine::WriteWTextureHeader(f, header)) {
        LOG_ERROR(Asset, "Failed to write .wtexture: {}", outputPath.c_str());
        return;
    }
    f.write(reinterpret_cast<const char*>(compressed.Data()), static_cast<std::streamsize>(realSize));

    LOG_INFO(Asset, "Wrote {}", outputPath.c_str());

    vkDestroyFence(context->device, graphicsFence, nullptr);
    vkDestroyCommandPool(context->device, graphicsCommandPool, nullptr);
}

void CreateSMAATextures(Core::MemoryManager* memoryManager,
                        Core::Path outputAreaPath,
                        Core::Path outputSearchPath,
                        Engine::TextureID areaTextureId,
                        Engine::TextureID searchTextureId,
                        Render::VulkanContext* context,
                        Render::ResourceManager* resourceManager,
                        Render::PipelineManager* pipelineManager,
                        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback)
{
    if (outputAreaPath.Exists() && outputSearchPath.Exists()) {
        LOG_INFO(Asset, "Skipping SMAA texture generation, files already exist: {} {}", outputAreaPath.c_str(), outputSearchPath.c_str());
        return;
    }

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

    // === Area Texture (160x560, R8G8_UNORM) ===
    constexpr uint32_t AREA_W = 160;
    constexpr uint32_t AREA_H = 560;
    constexpr VkDeviceSize areaByteSize = AREA_W * AREA_H * sizeof(uint8_t) * 2;

    VkImageCreateInfo areaImageInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R8G8_UNORM,
        {AREA_W, AREA_H, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    );
    Render::AllocatedImage areaImage = Render::AllocatedImage::CreateAllocatedImage(context, areaImageInfo);

    VkImageViewCreateInfo areaViewInfo = Render::VkHelpers::ImageViewCreateInfo(
        areaImage.handle,
        VK_FORMAT_R8G8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    areaViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    Render::ImageView areaImageView = Render::ImageView::CreateImageView(context, areaViewInfo);
    bool success = resourceManager->smaaLookupGenerateResources.WriteDescriptor(0, {nullptr, areaImageView.handle, VK_IMAGE_LAYOUT_GENERAL});
    assert(success);

    startGraphicsRecording();

    VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
        areaImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(graphicsCmd, &depInfo);

    //
    {
        SMAAAreaGeneratePushConstant pc{.targetIndex = 0};

        Core::Array<VkDescriptorBufferBindingInfoEXT, 1> bindings{resourceManager->smaaLookupGenerateResources.GetBindingInfo()};
        uint32_t bindingIndex{0u};
        VkDeviceSize bindingOffset{0};
        vkCmdBindDescriptorBuffersEXT(graphicsCmd, bindings.Size(), bindings.Data());

        const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_area_generate"));
        if (!pipelineEntry) {
            LOG_ERROR(Asset, "\"smaa_area_generate\" pipeline doesn't exist");
            return;
        }
        vkCmdBindPipeline(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(graphicsCmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdSetDescriptorBufferOffsetsEXT(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);
        vkCmdDispatch(graphicsCmd,
                      (AREA_W + SMAA_AREA_GENERATION_DISPATCH_X - 1) / SMAA_AREA_GENERATION_DISPATCH_X,
                      (AREA_H + SMAA_AREA_GENERATION_DISPATCH_Y - 1) / SMAA_AREA_GENERATION_DISPATCH_Y,
                      1);
        graphicsSubmitAndWait(true);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        areaImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    depInfo = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(graphicsCmd, &depInfo);

    Render::AllocatedBuffer areaStagingBuffer = Render::AllocatedBuffer::CreateAllocatedReceivingBuffer(context, areaByteSize);
    VkBufferImageCopy areaCopyRegion = {};
    areaCopyRegion.bufferOffset = 0;
    areaCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    areaCopyRegion.imageSubresource.mipLevel = 0;
    areaCopyRegion.imageSubresource.baseArrayLayer = 0;
    areaCopyRegion.imageSubresource.layerCount = 1;
    areaCopyRegion.imageExtent = {AREA_W, AREA_H, 1};
    vkCmdCopyImageToBuffer(graphicsCmd, areaImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, areaStagingBuffer.handle, 1, &areaCopyRegion);
    graphicsSubmitAndWait(false);

    auto areaData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, areaByteSize);
    memcpy(areaData.Data(), areaStagingBuffer.allocationInfo.pMappedData, areaByteSize);

    // Write area texture to KTX2 + .wtexture
    {
        ktxTexture2* texture;
        ktxTextureCreateInfo createInfo{};
        createInfo.vkFormat = VK_FORMAT_R8G8_UNORM;
        createInfo.baseWidth = AREA_W;
        createInfo.baseHeight = AREA_H;
        createInfo.baseDepth = 1;
        createInfo.numDimensions = 2;
        createInfo.numLevels = 1;
        createInfo.numLayers = 1;
        createInfo.numFaces = 1;
        createInfo.isArray = KTX_FALSE;
        createInfo.generateMipmaps = KTX_FALSE;

        ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
        if (result != KTX_SUCCESS) {
            LOG_ERROR(Asset, "Failed to create KTX texture for SMAA area texture");
            return;
        }

        ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, areaData.Data(), areaData.Size());

        const char writer[] = "WillEngine";
        ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

        ktx_uint8_t* ktxBytes{nullptr};
        ktx_size_t ktxSize{0};
        result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
        ktxTexture_Destroy(ktxTexture(texture));
        if (result != KTX_SUCCESS) {
            LOG_ERROR(Asset, "Failed to serialise SMAA area texture to memory");
            return;
        }

        auto maxCompressedSize = Engine::CompressLZ4MaxSize(ktxSize);
        auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
        size_t realSize = Engine::CompressLZ4(ktxBytes, ktxSize, compressed.Data(), compressed.Size());
        free(ktxBytes);

        Engine::WTextureHeader header{};
        header.textureId = areaTextureId.id;
        header.width = AREA_W;
        header.height = AREA_H;
        header.mipCount = 1;
        header.uncompressedSize = ktxSize;
        header.dataSize = realSize;

        strncpy_s(header.name, Engine::WTEXTURE_NAME_LENGTH, "smaa_area", Engine::WTEXTURE_NAME_LENGTH - 1);

        Platform::CreateDirectories(outputAreaPath.Parent().c_str());
        std::ofstream f(outputAreaPath.c_str(), std::ios::binary);
        if (!f || !Engine::WriteWTextureHeader(f, header)) {
            LOG_ERROR(Asset, "Failed to write .wtexture: {}", outputAreaPath.c_str());
            return;
        }
        f.write(reinterpret_cast<const char*>(compressed.Data()), static_cast<std::streamsize>(realSize));
        LOG_INFO(Asset, "Wrote {}", outputAreaPath.c_str());
    }

    // === Search Texture (64x16, R8_UNORM) ===
    constexpr uint32_t SEARCH_W = 64;
    constexpr uint32_t SEARCH_H = 16;
    constexpr VkDeviceSize searchByteSize = SEARCH_W * SEARCH_H * sizeof(uint8_t);

    VkImageCreateInfo searchImageInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R8_UNORM,
        {SEARCH_W, SEARCH_H, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    );
    Render::AllocatedImage searchImage = Render::AllocatedImage::CreateAllocatedImage(context, searchImageInfo);

    VkImageViewCreateInfo searchViewInfo = Render::VkHelpers::ImageViewCreateInfo(
        searchImage.handle,
        VK_FORMAT_R8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    searchViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    Render::ImageView searchImageView = Render::ImageView::CreateImageView(context, searchViewInfo);
    success = resourceManager->smaaSearchGenerateResources.WriteDescriptor(0, {nullptr, searchImageView.handle, VK_IMAGE_LAYOUT_GENERAL});
    assert(success);

    startGraphicsRecording();

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        searchImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
    );
    depInfo = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(graphicsCmd, &depInfo);

    {
        SMAASearchGeneratePushConstant pc{.targetIndex = 0};

        Core::Array<VkDescriptorBufferBindingInfoEXT, 1> bindings{resourceManager->smaaSearchGenerateResources.GetBindingInfo()};
        uint32_t bindingIndex{0u};
        VkDeviceSize bindingOffset{0};
        vkCmdBindDescriptorBuffersEXT(graphicsCmd, bindings.Size(), bindings.Data());

        const Render::PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_search_generate"));
        if (!pipelineEntry) {
            LOG_ERROR(Asset, "\"smaa_search_generate\" pipeline doesn't exist");
            return;
        }
        vkCmdBindPipeline(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(graphicsCmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdSetDescriptorBufferOffsetsEXT(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);
        vkCmdDispatch(graphicsCmd,
                      (SEARCH_W + SMAA_SEARCH_GENERATION_DISPATCH_X - 1) / SMAA_SEARCH_GENERATION_DISPATCH_X,
                      (SEARCH_H + SMAA_SEARCH_GENERATION_DISPATCH_Y - 1) / SMAA_SEARCH_GENERATION_DISPATCH_Y,
                      1);
        graphicsSubmitAndWait(true);
    }

    barrier = Render::VkHelpers::ImageMemoryBarrier(
        searchImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    depInfo = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(graphicsCmd, &depInfo);

    Render::AllocatedBuffer searchStagingBuffer = Render::AllocatedBuffer::CreateAllocatedReceivingBuffer(context, searchByteSize);
    VkBufferImageCopy searchCopyRegion = {};
    searchCopyRegion.bufferOffset = 0;
    searchCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    searchCopyRegion.imageSubresource.mipLevel = 0;
    searchCopyRegion.imageSubresource.baseArrayLayer = 0;
    searchCopyRegion.imageSubresource.layerCount = 1;
    searchCopyRegion.imageExtent = {SEARCH_W, SEARCH_H, 1};
    vkCmdCopyImageToBuffer(graphicsCmd, searchImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, searchStagingBuffer.handle, 1, &searchCopyRegion);
    graphicsSubmitAndWait(false);

    auto searchData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, searchByteSize);
    memcpy(searchData.Data(), searchStagingBuffer.allocationInfo.pMappedData, searchByteSize);

    // Write search texture to KTX2 + .wtexture
    {
        ktxTexture2* texture;
        ktxTextureCreateInfo createInfo{};
        createInfo.vkFormat = VK_FORMAT_R8_UNORM;
        createInfo.baseWidth = SEARCH_W;
        createInfo.baseHeight = SEARCH_H;
        createInfo.baseDepth = 1;
        createInfo.numDimensions = 2;
        createInfo.numLevels = 1;
        createInfo.numLayers = 1;
        createInfo.numFaces = 1;
        createInfo.isArray = KTX_FALSE;
        createInfo.generateMipmaps = KTX_FALSE;

        ktx_error_code_e result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
        if (result != KTX_SUCCESS) {
            LOG_ERROR(Asset, "Failed to create KTX texture for SMAA search texture");
            return;
        }

        ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, searchData.Data(), searchData.Size());

        const char writer[] = "WillEngine";
        ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

        ktx_uint8_t* ktxBytes{nullptr};
        ktx_size_t ktxSize{0};
        result = ktxTexture2_WriteToMemory(texture, &ktxBytes, &ktxSize);
        ktxTexture_Destroy(ktxTexture(texture));
        if (result != KTX_SUCCESS) {
            LOG_ERROR(Asset, "Failed to serialise SMAA search texture to memory");
            return;
        }

        auto maxCompressedSize = Engine::CompressLZ4MaxSize(ktxSize);
        auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
        size_t realSize = Engine::CompressLZ4(ktxBytes, ktxSize, compressed.Data(), compressed.Size());
        free(ktxBytes);

        Engine::WTextureHeader header{};
        header.textureId = searchTextureId.id;
        header.width = SEARCH_W;
        header.height = SEARCH_H;
        header.mipCount = 1;
        header.uncompressedSize = ktxSize;
        header.dataSize = realSize;

        strncpy_s(header.name, Engine::WTEXTURE_NAME_LENGTH, "smaa_search", Engine::WTEXTURE_NAME_LENGTH - 1);

        Platform::CreateDirectories(outputSearchPath.Parent().c_str());
        std::ofstream f(outputSearchPath.c_str(), std::ios::binary);
        if (!f || !Engine::WriteWTextureHeader(f, header)) {
            LOG_ERROR(Asset, "Failed to write .wtexture: {}", outputSearchPath.c_str());
            return;
        }
        f.write(reinterpret_cast<const char*>(compressed.Data()), static_cast<std::streamsize>(realSize));
        LOG_INFO(Asset, "Wrote {}", outputSearchPath.c_str());
    }
    vkDestroyFence(context->device, graphicsFence, nullptr);
    vkDestroyCommandPool(context->device, graphicsCommandPool, nullptr);
}
} // Editor
