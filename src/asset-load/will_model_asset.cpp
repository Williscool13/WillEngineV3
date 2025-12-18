//
// Created by William on 2025-12-18.
//

#include "will_model_asset.h"

#include "ktxvulkan.h"
#include "render/model/model_serialization.h"
#include "spdlog/spdlog.h"

namespace AssetLoad
{
WillModelAsset::WillModelAsset() = default;

WillModelAsset::~WillModelAsset() = default;

void WillModelAsset::TaskExecute()
{
    // if (!vulkanDeviceInfo) {
    //     const VkCommandPoolCreateInfo poolInfo = Renderer::VkHelpers::CommandPoolCreateInfo(vulkanContext->graphicsQueueFamily);
    //     VK_CHECK(vkCreateCommandPool(vulkanContext->device, &poolInfo, nullptr, &ktxTextureCommandPool));
    //     ktxVulkanFunctions vkFuncs{};
    //     vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    //     vkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    //     vulkanDeviceInfo = ktxVulkanDeviceInfo_CreateEx(vulkanContext->instance, vulkanContext->physicalDevice, vulkanContext->device, vulkanContext->graphicsQueue, ktxTextureCommandPool, nullptr,
    //                                                     &vkFuncs);
    // }

    if (!std::filesystem::exists(source)) {
        SPDLOG_ERROR("Failed to find path to willmodel - {}", source.string());
        SetState(LoadState::Failed);
        return;
    }

    Render::ModelReader reader(source);

    if (!reader.GetSuccessfullyLoaded()) {
        SetState(LoadState::Failed);
        return;
    }


    std::vector<uint8_t> modelBinData = reader.ReadFile("model.bin");

    size_t offset = 0;
    const auto* header = reinterpret_cast<Render::ModelBinaryHeader*>(modelBinData.data());
    offset += sizeof(Render::ModelBinaryHeader);

    auto readArray = [&]<typename T>(std::vector<T>& vec, uint32_t count) {
        vec.resize(count);
        if (count > 0) {
            std::memcpy(vec.data(), modelBinData.data() + offset, count * sizeof(T));
            offset += count * sizeof(T);
        }
    };


    const uint8_t* dataPtr = modelBinData.data() + offset;

    readArray(data.vertices, header->vertexCount);
    readArray(data.meshletVertices, header->meshletVertexCount);
    readArray(data.meshletTriangles, header->meshletTriangleCount);
    readArray(data.meshlets, header->meshletCount);
    readArray(data.primitives, header->primitiveCount);
    readArray(data.materials, header->materialCount);

    dataPtr = modelBinData.data() + offset;
    data.allMeshes.resize(header->meshCount);
    for (uint32_t i = 0; i < header->meshCount; i++) {
        ReadMeshInformation(dataPtr, data.allMeshes[i]);
    }

    data.nodes.resize(header->nodeCount);
    for (uint32_t i = 0; i < header->nodeCount; i++) {
        ReadNode(dataPtr, data.nodes[i]);
    }

    offset = dataPtr - modelBinData.data();
    readArray(data.nodeRemap, header->nodeRemapCount);

    dataPtr = modelBinData.data() + offset;
    data.animations.resize(header->animationCount);
    for (uint32_t i = 0; i < header->animationCount; i++) {
        ReadAnimation(dataPtr, data.animations[i]);
    }

    offset = dataPtr - modelBinData.data();
    readArray(data.inverseBindMatrices, header->inverseBindMatrixCount);

    readArray(pendingSamplerInfos, header->samplerCount);


    uint32_t textureIndex = 0;
    while (true) {
        std::string textureName = fmt::format("textures/texture_{}.ktx2", textureIndex);
        if (!reader.HasFile(textureName)) {
            break;
        }

        std::vector<uint8_t> ktxData = reader.ReadFile(textureName);

        std::filesystem::create_directories("temp");
        std::string tempKtxPath = fmt::format("temp/loaded_texture_{}.ktx2", textureIndex);
        std::ofstream tempFile(tempKtxPath, std::ios::binary);
        tempFile.write(reinterpret_cast<const char*>(ktxData.data()), ktxData.size());
        tempFile.close();

        ktxTexture* loadedTexture = nullptr;
        KTX_error_code result = ktxTexture_CreateFromNamedFile(tempKtxPath.c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &loadedTexture);

        auto texture2 = reinterpret_cast<ktxTexture2*>(loadedTexture);
        if (ktxTexture2_NeedsTranscoding(texture2)) {
            ktx_transcode_fmt_e targetFormat = KTX_TTF_BC7_RGBA;
            result = ktxTexture2_TranscodeBasis(texture2, targetFormat, 0);
            if (result != KTX_SUCCESS) {
                SPDLOG_ERROR("Failed to transcode texture {}", textureIndex);
                pendingTextures.push_back(nullptr);
                ktxTexture_Destroy(loadedTexture);
                textureIndex++;
            }
        }

        pendingTextures.push_back(texture2);
        ktxTexture_Destroy(loadedTexture);
        textureIndex++;
    }

    // Upload to GPU - in ThreadExecute
    // ktxVulkanTexture vkTexture;
    // result = ktxTexture_VkUploadEx(loadedTexture, vulkanDeviceInfo, &vkTexture, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // VkImageViewCreateInfo viewInfo = Renderer::VkHelpers::ImageViewCreateInfo(vkTexture.image, vkTexture.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    // viewInfo.viewType = vkTexture.viewType;
    // viewInfo.subresourceRange.layerCount = vkTexture.layerCount;
    // viewInfo.subresourceRange.levelCount = vkTexture.levelCount;
    // Renderer::ImageView imageView = Render::ImageView::CreateImageView(vulkanContext.get(), viewInfo);
    // Renderer::AllocatedImage allocatedImage;
    // allocatedImage.handle = vkTexture.image;
    // // allocatedImage.allocation = vkTexture.deviceMemory;
    // allocatedImage.format = vkTexture.imageFormat;
    // allocatedImage.extent = {vkTexture.width, vkTexture.height, vkTexture.depth};
    // extractedModel.images.push_back(std::move(allocatedImage));
    // extractedModel.imageViews.push_back(std::move(imageView));
    // for (VkSamplerCreateInfo& sampler : samplerInfos) {
    //     data.samplers.push_back(Render::Sampler::CreateSampler(context.get(), sampler));
    // }

    data.name = "Loaded Model";
}

void WillModelAsset::ThreadExecute() {}
} // AssetLoad
