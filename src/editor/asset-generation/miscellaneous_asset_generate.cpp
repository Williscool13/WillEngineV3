//
// Created by William on 2026-02-17.
//

#include "miscellaneous_asset_generate.h"

#include <cstring>
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
#include "engine/resources/wimage_format.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/resource_manager.h"
#include "render/descriptors/vk_bindless_resources_storage.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_utils.h"
#include "smaa/Textures/AreaTex.h"
#include "smaa/Textures/SearchTex.h"

namespace Editor
{
bool WriteRawBytesWTexture(Core::MemoryManager* memoryManager, const char* outputPath, Engine::TextureID id, const char* name,
                           VkFormat format, uint32_t w, uint32_t h, const uint8_t* data, size_t dataBytes)
{
    const Engine::WImageDesc desc{format, w, h, 1, 1};
    const size_t blobSize = Engine::WImageBlobSize(desc);
    auto blob = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, blobSize);
    if (Engine::WImageBlobInit(blob.Data(), blob.Size(), desc) == 0) {
        LOG_ERROR(Asset, "Failed to lay out image blob for {}", name);
        return false;
    }
    assert(dataBytes == Engine::WImageFaceSize(blob.Data(), 0) && "Raw data does not match blob level size");
    memcpy(Engine::WImageFaceData(blob.Data(), 0, 0), data, dataBytes);

    auto maxCompressedSize = Engine::CompressMaxSize(Engine::DEFAULT_TEXTURE_COMPRESSION, blobSize);
    auto compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, maxCompressedSize);
    size_t realSize = Engine::Compress(Engine::DEFAULT_TEXTURE_COMPRESSION, blob.Data(), blobSize, compressed.Data(), compressed.Size());

    Engine::WTextureHeader header{};
    header.textureId = id.id;
    header.width = w;
    header.height = h;
    header.mipCount = 1;
    header.uncompressedSize = blobSize;
    header.dataSize = realSize;
    header.compressionType = Engine::DEFAULT_TEXTURE_COMPRESSION;
    header.category = Engine::TextureCategory::Builtin;
    strncpy_s(header.name, name, Engine::WTEXTURE_NAME_LENGTH - 1);

    const Core::Path path(outputPath);
    Core::Vector<std::byte> headerOut(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Engine::WriteWTextureHeader(headerOut, header);
    if (!Platform::WriteFile(path, headerOut.Data(), headerOut.Size()) ||
        !Platform::AppendFile(path, compressed.Data(), realSize)) {
        LOG_ERROR(Asset, "Failed to write .wtexture: {}", outputPath);
        return false;
    }

    LOG_INFO(Asset, "Wrote {}", outputPath);
    return true;
}

bool WriteSimpleRGBA8WTexture(Core::MemoryManager* memoryManager, const char* outputPath, Engine::TextureID id, const char* name, uint32_t w, uint32_t h, const uint8_t* rgba)
{
    return WriteRawBytesWTexture(memoryManager, outputPath, id, name, VK_FORMAT_R8G8B8A8_SRGB, w, h, rgba, static_cast<size_t>(w) * h * 4);
}

static bool BuiltinUpToDate(const Core::Path& path)
{
    if (!path.Exists()) {
        return false;
    }
    std::optional<Engine::WTextureHeader> header = Engine::ReadWTextureHeader(path);
    return header && header->category == Engine::TextureCategory::Builtin;
}

void CreateCriticalEngineResources(Core::MemoryManager* memoryManager)
{
    const Core::Path texturesPath = Platform::GetAssetPath() / "textures";

    const Core::Path whitePath = texturesPath / "white.wtexture";
    if (!BuiltinUpToDate(whitePath)) {
        constexpr uint8_t pixels[4] = {255, 255, 255, 255};
        WriteSimpleRGBA8WTexture(
            memoryManager,
            whitePath.c_str(),
            Engine::TextureID(fnv1a64("white", 5)), "engine_default_white", 1, 1, pixels
        );
    }

    const Core::Path errorPath = texturesPath / "error.wtexture";
    if (!BuiltinUpToDate(errorPath)) {
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
    if (BuiltinUpToDate(outputPath)) {
        LOG_INFO(Asset, "Skipping BRDF LUT generation, file already up to date: {}", outputPath.c_str());
        return;
    }

    const uint32_t submitFamily = context->computeQueue != VK_NULL_HANDLE ? context->computeQueueFamily : context->graphicsQueueFamily;
    VkCommandPoolCreateInfo graphicsPoolInfo = Render::VkHelpers::CommandPoolCreateInfo(submitFamily);
    VkCommandPool graphicsCommandPool;
    VK_CHECK(vkCreateCommandPool(context->device, &graphicsPoolInfo, context->HostAllocCallbacks(), &graphicsCommandPool));

    VkCommandBufferAllocateInfo graphicsCmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, graphicsCommandPool);
    VkCommandBuffer graphicsCmd;
    VK_CHECK(vkAllocateCommandBuffers(context->device, &graphicsCmdInfo, &graphicsCmd));

    VkFenceCreateInfo graphicsFenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence graphicsFence;
    VK_CHECK(vkCreateFence(context->device, &graphicsFenceInfo, context->HostAllocCallbacks(), &graphicsFence));

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

        const Render::PipelineEntry pipelineEntry = pipelineManager->GetPipelineEntrySnapshot(SID("ibl_brdf_lut"));
        if (pipelineEntry.pipeline == VK_NULL_HANDLE) {
            LOG_ERROR(Asset, "\"ibl_brdf_lut\" pipeline not ready");
            return;
        }
        vkCmdBindPipeline(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.pipeline);
        vkCmdPushConstants(graphicsCmd, pipelineEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdSetDescriptorBufferOffsetsEXT(graphicsCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry.layout, 0, bindings.Size(), &bindingIndex, &bindingOffset);
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


    // Copy back to CPU for blob serialization
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

    WriteRawBytesWTexture(memoryManager, outputPath.c_str(), textureId, "brdf_lut",
                          VK_FORMAT_R16G16_SFLOAT, LUT_SIZE, LUT_SIZE, lutData.Data(), lutByteSize);

    vkDestroyFence(context->device, graphicsFence, context->HostAllocCallbacks());
    vkDestroyCommandPool(context->device, graphicsCommandPool, context->HostAllocCallbacks());
}

static void GenerateBlueNoiseRanks(uint32_t size, uint32_t seed, Core::MemoryManager* memoryManager, uint8_t* out, uint32_t stride, uint32_t channel)
{
    const uint32_t count = size * size;
    auto energy = Core::HeapArray<float>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, count);
    auto placed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, count);
    memset(energy.Data(), 0, count * sizeof(float));
    memset(placed.Data(), 0, count);

    constexpr float sigma = 1.9f;
    constexpr int kernelRadius = 10;
    float kernel[2 * kernelRadius + 1][2 * kernelRadius + 1];
    for (int dy = -kernelRadius; dy <= kernelRadius; dy++) {
        for (int dx = -kernelRadius; dx <= kernelRadius; dx++) {
            kernel[dy + kernelRadius][dx + kernelRadius] = expf(-float(dx * dx + dy * dy) / (2.0f * sigma * sigma));
        }
    }

    uint32_t rng = seed;
    for (uint32_t rank = 0; rank < count; rank++) {
        rng ^= rng << 13u;
        rng ^= rng >> 17u;
        rng ^= rng << 5u;
        // Randomized scan start breaks energy ties without biasing early ranks toward the origin.
        const uint32_t offset = rng & (count - 1u);
        uint32_t best = 0;
        float bestEnergy = 3.4e38f;
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t p = (i + offset) & (count - 1u);
            if (placed[p] == 0 && energy[p] < bestEnergy) {
                bestEnergy = energy[p];
                best = p;
            }
        }
        placed[best] = 1;
        out[best * stride + channel] = static_cast<uint8_t>((rank * 255u + (count - 1u) / 2u) / (count - 1u));

        const int bx = static_cast<int>(best % size);
        const int by = static_cast<int>(best / size);
        for (int dy = -kernelRadius; dy <= kernelRadius; dy++) {
            const uint32_t y = static_cast<uint32_t>((by + dy + static_cast<int>(size)) % static_cast<int>(size));
            for (int dx = -kernelRadius; dx <= kernelRadius; dx++) {
                const uint32_t x = static_cast<uint32_t>((bx + dx + static_cast<int>(size)) % static_cast<int>(size));
                energy[y * size + x] += kernel[dy + kernelRadius][dx + kernelRadius];
            }
        }
    }
}

void CreateBlueNoiseTexture(Core::MemoryManager* memoryManager, Core::Path outputPath, Engine::TextureID textureId)
{
    if (BuiltinUpToDate(outputPath)) {
        LOG_INFO(Asset, "Skipping blue noise generation, file already up to date: {}", outputPath.c_str());
        return;
    }

    constexpr uint32_t BLUE_NOISE_SIZE = 128;
    auto pixels = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, BLUE_NOISE_SIZE * BLUE_NOISE_SIZE * 2);
    GenerateBlueNoiseRanks(BLUE_NOISE_SIZE, 0x9E3779B9u, memoryManager, pixels.Data(), 2, 0);
    GenerateBlueNoiseRanks(BLUE_NOISE_SIZE, 0x85EBCA6Bu, memoryManager, pixels.Data(), 2, 1);

    WriteRawBytesWTexture(memoryManager, outputPath.c_str(), textureId, "blue_noise",
                          VK_FORMAT_R8G8_UNORM, BLUE_NOISE_SIZE, BLUE_NOISE_SIZE,
                          pixels.Data(), BLUE_NOISE_SIZE * BLUE_NOISE_SIZE * 2);
}

void CreateSMAATextures(Core::MemoryManager* memoryManager,
                        Core::Path outputAreaPath,
                        Core::Path outputSearchPath,
                        Engine::TextureID areaTextureId,
                        Engine::TextureID searchTextureId)
{
    if (BuiltinUpToDate(outputAreaPath) && BuiltinUpToDate(outputSearchPath)) {
        LOG_INFO(Asset, "Skipping SMAA texture generation, files already up to date: {} {}", outputAreaPath.c_str(), outputSearchPath.c_str());
        return;
    }

    WriteRawBytesWTexture(memoryManager, outputAreaPath.c_str(), areaTextureId, "smaa_area",
                          VK_FORMAT_R8G8_UNORM, AREATEX_WIDTH, AREATEX_HEIGHT,
                          areaTexBytes, AREATEX_SIZE);


    WriteRawBytesWTexture(memoryManager, outputSearchPath.c_str(), searchTextureId, "smaa_search",
                          VK_FORMAT_R8_UNORM, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
                          searchTexBytes, SEARCHTEX_SIZE);
}
} // Editor
