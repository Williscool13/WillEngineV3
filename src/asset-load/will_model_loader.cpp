//
// Created by William on 2025-12-19.
//

#include "will_model_loader.h"

#include <fstream>

#include "ktxvulkan.h"
#include "render/model/model_serialization.h"

namespace AssetLoad
{
void LoadModelTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
{
    if (modelLoader) {
        modelLoader->TaskImplementation();
    }
}

WillModelLoader::WillModelLoader()
{
    uploadStaging = std::make_unique<UploadStaging>();
    loadModelTask = std::make_unique<LoadModelTask>();
}

WillModelLoader::~WillModelLoader() = default;

void WillModelLoader::Reset()
{
    rawData.Reset();
    loadState = WillModelLoadState::Idle;
    willModelHandle = Render::WillModelHandle::INVALID;
    model = nullptr;
    pendingSamplerInfos.clear();
    for (ktxTexture2* texture : pendingTextures) {
        ktxTexture2_Destroy(texture);
    }
    pendingTextures.clear();
}

WillModelLoader::TaskState WillModelLoader::TaskExecute(enki::TaskScheduler* scheduler, LoadModelTask* task)
{
    if (taskState == TaskState::NotStarted) {
        task->modelLoader = this;
        taskState = TaskState::InProgress;
        scheduler->AddTaskSetToPipe(task);
    }

    if (task->GetIsComplete()) {
        return taskState;
    }

    return TaskState::InProgress;
}

void WillModelLoader::TaskImplementation()
{
    // todo: tracy profile this step
    if (!std::filesystem::exists(model->source)) {
        SPDLOG_ERROR("Failed to find path to willmodel - {}", model->name);
        taskState = TaskState::Failed;
        return;
    }

    Render::ModelReader reader = Render::ModelReader(model->source);

    if (!reader.GetSuccessfullyLoaded()) {
        SPDLOG_ERROR("Failed to load willmodel - {}", model->name);
        taskState = TaskState::Failed;
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

    readArray(rawData.vertices, header->vertexCount);
    readArray(rawData.meshletVertices, header->meshletVertexCount);
    readArray(rawData.meshletTriangles, header->meshletTriangleCount);
    readArray(rawData.meshlets, header->meshletCount);
    readArray(rawData.primitives, header->primitiveCount);
    readArray(rawData.materials, header->materialCount);

    dataPtr = modelBinData.data() + offset;
    rawData.allMeshes.resize(header->meshCount);
    for (uint32_t i = 0; i < header->meshCount; i++) {
        ReadMeshInformation(dataPtr, rawData.allMeshes[i]);
    }

    rawData.nodes.resize(header->nodeCount);
    for (uint32_t i = 0; i < header->nodeCount; i++) {
        ReadNode(dataPtr, rawData.nodes[i]);
    }

    offset = dataPtr - modelBinData.data();
    readArray(rawData.nodeRemap, header->nodeRemapCount);

    dataPtr = modelBinData.data() + offset;
    rawData.animations.resize(header->animationCount);
    for (uint32_t i = 0; i < header->animationCount; i++) {
        ReadAnimation(dataPtr, rawData.animations[i]);
    }

    offset = dataPtr - modelBinData.data();
    readArray(rawData.inverseBindMatrices, header->inverseBindMatrixCount);

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
    rawData.name = "Loaded Model";
    taskState = TaskState::Complete;
}

WillModelLoader::ThreadState WillModelLoader::ThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager)
{
    // Resource Creation
    {
        if (pendingSamplerInfos.size() > 0) {
            for (VkSamplerCreateInfo& sampler : pendingSamplerInfos) {
                model->modelData.samplers.push_back(Render::Sampler::CreateSampler(context, sampler));
            }
        }

        if (pendingTextures.size() > 0) {
            // for (auto& texture : pendingTextures) {
            //     ktxVulkanTexture vkTexture;
            //     auto result = ktxTexture_VkUploadEx(texture, vulkanDeviceInfo, &vkTexture, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            //
            //     VkImageViewCreateInfo viewInfo = Renderer::VkHelpers::ImageViewCreateInfo(vkTexture.image, vkTexture.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
            //     viewInfo.viewType = vkTexture.viewType;
            //     viewInfo.subresourceRange.layerCount = vkTexture.layerCount;
            //     viewInfo.subresourceRange.levelCount = vkTexture.levelCount;
            //     Renderer::ImageView imageView = Renderer::VkResources::CreateImageView(vulkanContext.get(), viewInfo);
            //
            //     Renderer::AllocatedImage allocatedImage;
            //     allocatedImage.handle = vkTexture.image;
            //     // allocatedImage.allocation = vkTexture.deviceMemory;
            //     allocatedImage.format = vkTexture.imageFormat;
            //     allocatedImage.extent = {vkTexture.width, vkTexture.height, vkTexture.depth};
            //
            //     extractedModel.images.push_back(std::move(allocatedImage));
            //     extractedModel.imageViews.push_back(std::move(imageView));
            //
            //     ktxTexture_Destroy(loadedTexture);
            // }
        }

        // upload GPu resources. if full, just


    }


    // Materials
    {
        // Samplers
        auto remapSamplers = [](auto& indices, const std::vector<Render::BindlessSamplerHandle>& map) {
            indices.x = indices.x >= 0 ? map[indices.x].index : 0;
            indices.y = indices.y >= 0 ? map[indices.y].index : 0;
            indices.z = indices.z >= 0 ? map[indices.z].index : 0;
            indices.w = indices.w >= 0 ? map[indices.w].index : 0;
        };

        model->modelData.samplerIndexToDescriptorBufferIndexMap.resize(rawData.pendingSamplerInfos.size());
        for (int32_t i = 0; i < model->modelData.samplers.size(); ++i) {
            model->modelData.samplerIndexToDescriptorBufferIndexMap[i] = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateSampler(model->modelData.samplers[i].handle);
        }

        for (MaterialProperties& material : rawData.materials) {
            remapSamplers(material.textureSamplerIndices, model->modelData.samplerIndexToDescriptorBufferIndexMap);
            remapSamplers(material.textureSamplerIndices2, model->modelData.samplerIndexToDescriptorBufferIndexMap);
        }

        // Textures
        auto remapTextures = [](auto& indices, const std::vector<Render::BindlessTextureHandle>& map) {
            indices.x = indices.x >= 0 ? map[indices.x].index : 0;
            indices.y = indices.y >= 0 ? map[indices.y].index : 0;
            indices.z = indices.z >= 0 ? map[indices.z].index : 0;
            indices.w = indices.w >= 0 ? map[indices.w].index : 0;
        };

        for (int32_t i = 0; i < model->modelData.imageViews.size(); ++i) {
            // todo: if image failed to be transcoded/loaded, need to use default/error image.
            model->modelData.textureIndexToDescriptorBufferIndexMap[i] = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateTexture({
                .imageView = model->modelData.imageViews[i].handle,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
        }

        for (MaterialProperties& material : rawData.materials) {
            remapTextures(material.textureImageIndices, model->modelData.textureIndexToDescriptorBufferIndexMap);
            remapTextures(material.textureImageIndices2, model->modelData.textureIndexToDescriptorBufferIndexMap);
        }

        // todo: materials will be dynamic here, no longer uploading to staging/copy
        // Materials
        // size_t sizeMaterials = meshletModel_.materials.size() * sizeof(Renderer::MaterialProperties);
        // model.materialAllocation = materialBufferAllocator.allocate(sizeMaterials);
        // memcpy(static_cast<char*>(materialBuffer.allocationInfo.pMappedData) + model.materialAllocation.offset, meshletModel_.materials.data(), sizeMaterials);
    }




    return ThreadState::Complete;
}
} // AssetLoad
