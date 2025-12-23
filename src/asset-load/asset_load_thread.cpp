//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "will_model_loader.h"
#include "render/model/will_model_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
AssetLoadThread::AssetLoadThread() = default;

AssetLoadThread::~AssetLoadThread()
{
    if (context) {
        vkDestroyCommandPool(context->device, commandPool, nullptr);
    }
}

void AssetLoadThread::Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));
    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(ASSET_LOAD_ASYNC_COUNT, commandPool);
    std::array<VkCommandBuffer, ASSET_LOAD_ASYNC_COUNT + 1> commandBuffers{};
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, commandBuffers.data()));

    for (int32_t i = 0; i < assetLoadSlots.size(); ++i) {
        assetLoadSlots[i].uploadStaging->Initialize(context, commandBuffers[i]);
    }

    VkCommandBuffer customCommandBuffer = commandBuffers[ASSET_LOAD_ASYNC_COUNT];

    CreateDefaultResources(customCommandBuffer);
}

void AssetLoadThread::Start()
{
    bShouldExit.store(false, std::memory_order_release);

    uint32_t assetLoadThreadNum = scheduler->GetNumTaskThreads() - 2;
    pinnedTask = std::make_unique<enki::LambdaPinnedTask>(
        assetLoadThreadNum,
        [this] { ThreadMain(); }
    );

    scheduler->AddPinnedTask(pinnedTask.get());
}

void AssetLoadThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
}

void AssetLoadThread::Join() const
{
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void AssetLoadThread::RequestLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr)
{
    modelLoadQueue.push({willmodelHandle, willModelPtr});
}

bool AssetLoadThread::ResolveLoads(WillModelComplete& modelComplete)
{
    return modelCompleteLoadQueue.pop(modelComplete);
}

void AssetLoadThread::RequestUnLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr)
{
    modelUnloadQueue.push({willmodelHandle, willModelPtr});
}

bool AssetLoadThread::ResolveUnload(WillModelComplete& modelComplete)
{
    return modelCompleteUnloadQueue.pop(modelComplete);
}

void AssetLoadThread::ThreadMain()
{
    while (!bShouldExit.load(std::memory_order_acquire)) {
        // Try to start
        for (size_t i = 0; i < ASSET_LOAD_ASYNC_COUNT; ++i) {
            if (!loaderActive[i]) {
                WillModelLoadRequest loadRequest{};
                if (modelLoadQueue.pop(loadRequest)) {
                    assetLoadSlots[i].loadState = WillModelLoadState::Idle;
                    assetLoadSlots[i].willModelHandle = loadRequest.willModelHandle;
                    assetLoadSlots[i].model = loadRequest.model;
                    loaderActive[i] = true;
                }
            }
        }

        // Resolve existing currently loading stuff
        for (size_t i = 0; i < ASSET_LOAD_ASYNC_COUNT; ++i) {
            if (loaderActive[i]) {
                WillModelLoader& assetLoad = assetLoadSlots[i];
                switch (assetLoad.loadState) {
                    case WillModelLoadState::Idle:
                    {
                        assetLoad.TaskExecute(scheduler, assetLoad.loadModelTask.get());
                        assetLoad.loadState = WillModelLoadState::TaskExecuting;
                        SPDLOG_INFO("Started task for {}", assetLoad.model->name);
                    }
                    break;
                    case WillModelLoadState::TaskExecuting:
                    {
                        WillModelLoader::TaskState res = assetLoad.TaskExecute(scheduler, assetLoad.loadModelTask.get());
                        if (res == WillModelLoader::TaskState::Failed) {
                            assetLoad.loadState = WillModelLoadState::Failed;
                            SPDLOG_WARN("Failed task for {}", assetLoad.model->name);
                        }
                        else if (res == WillModelLoader::TaskState::Complete) {
                            SPDLOG_INFO("Finished task for {}", assetLoad.model->name);
                            const bool preRes = assetLoad.PreThreadExecute(context, resourceManager);
                            if (preRes) {
                                assetLoad.loadState = WillModelLoadState::ThreadExecuting;
                                assetLoad.ThreadExecute(context, resourceManager);
                                SPDLOG_INFO("Started thread execute for {}", assetLoad.model->name);
                            }
                            else {
                                assetLoad.loadState = WillModelLoadState::Failed;
                                SPDLOG_INFO("Failed pre thread execute for {}", assetLoad.model->name);
                            }
                        }
                    }
                    break;
                    case WillModelLoadState::ThreadExecuting:
                    {
                        WillModelLoader::ThreadState res = assetLoad.ThreadExecute(context, resourceManager);
                        if (res == WillModelLoader::ThreadState::Complete) {
                            const bool postRes = assetLoad.PostThreadExecute(context, resourceManager);
                            if (postRes) {
                                assetLoad.loadState = WillModelLoadState::Loaded;
                                SPDLOG_INFO("Successfully loaded {}", assetLoad.model->name);
                            }
                            else {
                                assetLoad.loadState = WillModelLoadState::Failed;
                                SPDLOG_INFO("Failed post thread execute for {}", assetLoad.model->name);
                            }
                        }
                    }
                    break;
                    default:
                        break;
                }

                if (assetLoad.loadState == WillModelLoadState::Loaded || assetLoad.loadState == WillModelLoadState::Failed) {
                    bool success = assetLoad.loadState == WillModelLoadState::Loaded;
                    modelCompleteLoadQueue.push({assetLoad.willModelHandle, assetLoad.model, success});
                    loaderActive[i] = false;

                    assetLoad.Reset();
                }
            }
        }


        // Resolve unloads
        WillModelLoadRequest unloadRequest{};
        if (modelUnloadQueue.pop(unloadRequest)) {
            OffsetAllocator::Allocator* selectedAllocator;
            if (unloadRequest.model->modelData.bIsSkinned) {
                selectedAllocator = &resourceManager->skinnedVertexBufferAllocator;
            }
            else {
                selectedAllocator = &resourceManager->vertexBufferAllocator;
            }

            selectedAllocator->free(unloadRequest.model->modelData.vertexAllocation);
            resourceManager->meshletVertexBufferAllocator.free(unloadRequest.model->modelData.meshletVertexAllocation);
            resourceManager->meshletTriangleBufferAllocator.free(unloadRequest.model->modelData.meshletTriangleAllocation);
            resourceManager->meshletBufferAllocator.free(unloadRequest.model->modelData.meshletAllocation);
            resourceManager->primitiveBufferAllocator.free(unloadRequest.model->modelData.primitiveAllocation);
            unloadRequest.model->modelData.vertexAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletVertexAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletTriangleAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.primitiveAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;

            modelCompleteUnloadQueue.push({unloadRequest.willModelHandle, unloadRequest.model, true});
        }
    }
}

void AssetLoadThread::CreateDefaultResources(VkCommandBuffer cmd)
{
    /*const uint32_t white = packUnorm4x8(glm::vec4(1, 1, 1, 1));
    const uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    const uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels{}; //for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }

    UploadStagingHandle uploadStagingHandle = GetAvailableStaging();
    UploadStaging* currentUploadStaging = &uploadStagingDatas[uploadStagingHandle.index];
    const VkCommandBufferBeginInfo cmdBeginInfo = Render::VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // White texture
    constexpr size_t whiteSize = 4;
    constexpr VkExtent3D whiteExtent = {1, 1, 1};
    OffsetAllocator::Allocation whiteImageAllocation = currentUploadStaging->GetStagingAllocator().allocate(whiteSize);
    char* whiteBufferOffset = static_cast<char*>(currentUploadStaging->GetStagingBuffer().allocationInfo.pMappedData) + whiteImageAllocation.offset;
    memcpy(whiteBufferOffset, &white, whiteSize);
    VkImageCreateInfo whiteImageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R8G8B8A8_UNORM, whiteExtent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    whiteImage = Renderer::VkResources::CreateAllocatedImage(context, whiteImageCreateInfo);
    UploadTexture(context, newModel, currentUploadStaging, whiteImage, whiteExtent, whiteImageAllocation.offset);
    VkImageViewCreateInfo whiteImageViewCreateInfo = Renderer::VkHelpers::ImageViewCreateInfo(whiteImage.handle, whiteImage.format, VK_IMAGE_ASPECT_COLOR_BIT);
    whiteImageView = Renderer::VkResources::CreateImageView(context, whiteImageViewCreateInfo);
    whiteImageDescriptorIndex = resourceManager->bindlessResourcesDescriptorBuffer.AllocateTexture({
        .imageView = whiteImageView.handle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });
    assert(whiteImageDescriptorIndex == 0);

    // Error texture (magenta/black checkerboard)
    constexpr size_t errorSize = sizeof(pixels);
    constexpr VkExtent3D errorExtent = {16, 16, 1};
    OffsetAllocator::Allocation errorImageAllocation = currentUploadStaging->stagingAllocator.allocate(errorSize);
    char* errorBufferOffset = static_cast<char*>(currentUploadStaging->stagingBuffer.allocationInfo.pMappedData) + errorImageAllocation.offset;
    memcpy(errorBufferOffset, pixels.data(), errorSize);
    VkImageCreateInfo errorImageCreateInfo = Renderer::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R8G8B8A8_UNORM, errorExtent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    errorImage = Renderer::VkResources::CreateAllocatedImage(context, errorImageCreateInfo);
    UploadTexture(context, newModel, currentUploadStaging, errorImage, errorExtent, errorImageAllocation.offset);
    VkImageViewCreateInfo errorImageViewCreateInfo = Renderer::VkHelpers::ImageViewCreateInfo(errorImage.handle, errorImage.format, VK_IMAGE_ASPECT_COLOR_BIT);
    errorImageView = Renderer::VkResources::CreateImageView(context, errorImageViewCreateInfo);
    errorImageDescriptorIndex = resourceManager->bindlessResourcesDescriptorBuffer.AllocateTexture({
        .imageView = errorImageView.handle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });
    assert(errorImageDescriptorIndex == 1);

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };
    defaultSamplerLinear = Renderer::VkResources::CreateSampler(context, samplerInfo);
    samplerLinearDescriptorIndex = resourceManager->bindlessResourcesDescriptorBuffer.AllocateSampler(defaultSamplerLinear.handle);
    assert(samplerLinearDescriptorIndex == 0);

    StartUploadStaging(*currentUploadStaging);
    modelsInProgress.push_back({defaultResourcesHandle});*/
}
} // AssetLoad
