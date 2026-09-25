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
    if (!Platform::FileExists(path)) {
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
            Engine::TextureID(14905071471194202630ULL), "engine_default_white", 1, 1, pixels
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
            Engine::TextureID(11489899660447141169ULL), "engine_default_error", 4, 4, pixels
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
        VK_CHECK(vkWaitForFences(context->device, 1, &graphicsFence, VK_TRUE, UINT64_MAX));
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

        const Render::PipelineEntry pipelineEntry = pipelineManager->GetPipelineEntrySnapshot("ibl_brdf_lut"_sid);
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

static constexpr uint32_t STBN_SIZE = 128;
static constexpr uint32_t STBN_SLICES = 64;
static constexpr uint32_t STBN_ATLAS_TILES = 8;
static constexpr uint32_t STBN_ATLAS_SIZE = STBN_SIZE * STBN_ATLAS_TILES;

static void STBNHeapSiftDown(float* keys, uint32_t* ids, uint32_t count, uint32_t i)
{
    while (true) {
        const uint32_t l = 2u * i + 1u;
        if (l >= count) {
            return;
        }
        const uint32_t r = l + 1u;
        const uint32_t m = (r < count && keys[r] < keys[l]) ? r : l;
        if (keys[i] <= keys[m]) {
            return;
        }
        std::swap(keys[i], keys[m]);
        std::swap(ids[i], ids[m]);
        i = m;
    }
}

/** Spatiotemporal void-and-cluster (Wolfe et al. 2022), toroidal in x, y and t; placement order becomes the value. */
static void GenerateSTBNRanks(uint32_t seed, Core::MemoryManager* memoryManager, uint8_t* atlas, uint32_t channel)
{
    constexpr uint32_t sliceTexels = STBN_SIZE * STBN_SIZE;
    constexpr uint32_t count = sliceTexels * STBN_SLICES;
    constexpr int spatialRadius = 10;
    constexpr int temporalRadius = 10;
    // exp(-1 / (2 * 1.9^2)): powers by repeated multiplication instead of expf, so every compiler and CRT produces the same texture.
    constexpr float gaussianRatio = 0.870659649f;
    constexpr int maxDistanceSq = 2 * spatialRadius * spatialRadius;
    float gaussian[maxDistanceSq + 1];
    gaussian[0] = 1.0f;
    for (int i = 1; i <= maxDistanceSq; i++) {
        gaussian[i] = gaussian[i - 1] * gaussianRatio;
    }

    auto energyArray = Core::HeapArray<float>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, count);
    auto keyArray = Core::HeapArray<float>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, count);
    auto idArray = Core::HeapArray<uint32_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, count);
    float* energy = energyArray.Data();
    float* keys = keyArray.Data();
    uint32_t* ids = idArray.Data();

    float spatialKernel[2 * spatialRadius + 1][2 * spatialRadius + 1];
    for (int dy = -spatialRadius; dy <= spatialRadius; dy++) {
        for (int dx = -spatialRadius; dx <= spatialRadius; dx++) {
            spatialKernel[dy + spatialRadius][dx + spatialRadius] = gaussian[dx * dx + dy * dy];
        }
    }
    float temporalKernel[2 * temporalRadius + 1];
    for (int dt = -temporalRadius; dt <= temporalRadius; dt++) {
        temporalKernel[dt + temporalRadius] = gaussian[dt * dt];
    }

    // Sub-kernel jitter breaks the all-zero ties in random order instead of scan order.
    uint32_t rng = seed;
    for (uint32_t i = 0; i < count; i++) {
        rng ^= rng << 13u;
        rng ^= rng >> 17u;
        rng ^= rng << 5u;
        energy[i] = float(rng >> 8) * (1e-6f / float(1u << 24));
        keys[i] = energy[i];
        ids[i] = i;
    }
    for (uint32_t i = count / 2u; i-- > 0u;) {
        STBNHeapSiftDown(keys, ids, count, i);
    }

    // Energy only grows, so a heap key is never above its texel's energy; a key below it is stale and goes back in at the current value.
    uint32_t heapCount = count;
    uint32_t rank = 0;
    while (heapCount > 0u) {
        const uint32_t best = ids[0];
        if (keys[0] < energy[best]) {
            keys[0] = energy[best];
            STBNHeapSiftDown(keys, ids, heapCount, 0);
            continue;
        }
        heapCount--;
        keys[0] = keys[heapCount];
        ids[0] = ids[heapCount];
        STBNHeapSiftDown(keys, ids, heapCount, 0);

        const int bx = static_cast<int>(best % STBN_SIZE);
        const int by = static_cast<int>((best / STBN_SIZE) % STBN_SIZE);
        const int bt = static_cast<int>(best / sliceTexels);
        const uint32_t ax = (static_cast<uint32_t>(bt) % STBN_ATLAS_TILES) * STBN_SIZE + static_cast<uint32_t>(bx);
        const uint32_t ay = (static_cast<uint32_t>(bt) / STBN_ATLAS_TILES) * STBN_SIZE + static_cast<uint32_t>(by);
        atlas[(ay * STBN_ATLAS_SIZE + ax) * 2u + channel] = static_cast<uint8_t>((static_cast<uint64_t>(rank) * 256u) / count);
        rank++;

        const int size = static_cast<int>(STBN_SIZE);
        float* slice = energy + static_cast<size_t>(bt) * sliceTexels;
        for (int dy = -spatialRadius; dy <= spatialRadius; dy++) {
            const int y = (by + dy + size) % size;
            for (int dx = -spatialRadius; dx <= spatialRadius; dx++) {
                const int x = (bx + dx + size) % size;
                slice[y * size + x] += spatialKernel[dy + spatialRadius][dx + spatialRadius];
            }
        }
        const int slices = static_cast<int>(STBN_SLICES);
        const size_t texel = static_cast<size_t>(by) * STBN_SIZE + static_cast<size_t>(bx);
        for (int dt = -temporalRadius; dt <= temporalRadius; dt++) {
            if (dt == 0) {
                continue;
            }
            const int t = (bt + dt + slices) % slices;
            energy[static_cast<size_t>(t) * sliceTexels + texel] += temporalKernel[dt + temporalRadius];
        }
    }
}

void CreateBlueNoiseTexture(Core::MemoryManager* memoryManager, Core::Path outputPath, Engine::TextureID textureId)
{
    if (BuiltinUpToDate(outputPath)) {
        std::optional<Engine::WTextureHeader> header = Engine::ReadWTextureHeader(outputPath);
        if (header && header->width == STBN_ATLAS_SIZE && header->height == STBN_ATLAS_SIZE) {
            LOG_INFO(Asset, "Skipping blue noise generation, file already up to date: {}", outputPath.c_str());
            return;
        }
    }

    auto pixels = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, STBN_ATLAS_SIZE * STBN_ATLAS_SIZE * 2);
    GenerateSTBNRanks(0x9E3779B9u, memoryManager, pixels.Data(), 0);
    GenerateSTBNRanks(0x85EBCA6Bu, memoryManager, pixels.Data(), 1);

    WriteRawBytesWTexture(memoryManager, outputPath.c_str(), textureId, "blue_noise",
                          VK_FORMAT_R8G8_UNORM, STBN_ATLAS_SIZE, STBN_ATLAS_SIZE,
                          pixels.Data(), STBN_ATLAS_SIZE * STBN_ATLAS_SIZE * 2);
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
