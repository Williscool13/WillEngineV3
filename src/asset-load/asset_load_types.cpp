//
// Created by William on 2025-12-18.
//

#include "asset_load_types.h"

#include <tracy/Tracy.hpp>

#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
UploadStaging::UploadStaging() = default;

UploadStaging::~UploadStaging()
{
    // RAII will take care of staging buffer
}

void UploadStaging::Initialize(Render::VulkanContext* context, size_t stagingSize)
{
    stagingBuffer = std::move(Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, stagingSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 16));
    stagingBuffer.address = Render::VkHelpers::GetDeviceAddress(context->device, stagingBuffer.handle);
    stagingAllocator = Core::LinearAllocator{stagingSize, "AssetUploadStaging"};
}

void UploadStaging::Destroy()
{
    stagingBuffer = {};
    stagingAllocator.Reset();
}

void UploadStagingDepot::Initialize(Render::VulkanContext* _context, uint64_t _budgetBytes)
{
    context = _context;
    budgetBytes = _budgetBytes;
    for (UploadStaging& staging : stagings) {
        freeSlots.PushBack(&staging);
    }
}

UploadStaging* UploadStagingDepot::CheckOut(uint64_t contentBytes)
{
    const uint64_t grant = contentBytes < UPLOAD_STAGING_MIN_SIZE ? UPLOAD_STAGING_MIN_SIZE : (contentBytes > UPLOAD_STAGING_MAX_SIZE ? UPLOAD_STAGING_MAX_SIZE : contentBytes);

    UploadStaging* staging = nullptr;
    {
        std::lock_guard lock(mutex);
        if (usedBytes + grant > budgetBytes || freeSlots.IsEmpty()) { return nullptr; }
        staging = freeSlots[freeSlots.Size() - 1];
        freeSlots.PopBack();
        usedBytes += grant;
    }

    ZoneScopedN("UploadStagingDepot Create Staging Buffer");
    staging->Initialize(context, grant);
    return staging;
}

void UploadStagingDepot::Return(UploadStaging* staging)
{
    const uint64_t grant = staging->GetStagingAllocator().GetCapacity();
    {
        ZoneScopedN("UploadStagingDepot Destroy Staging Buffer");
        staging->Destroy();
    }
    std::lock_guard lock(mutex);
    freeSlots.PushBack(staging);
    usedBytes -= grant;
}

void UploadStagingDepot::SetBudgetBytes(uint64_t newBudget)
{
    assert((newBudget == UPLOAD_STAGING_BUDGET_DEFAULT || newBudget == UPLOAD_STAGING_BUDGET_LOADING_SCREEN) && "Budget must be a config preset");
    std::lock_guard lock(mutex);
    budgetBytes = newBudget;
}

void SubmitContextDepot::Initialize(Render::VulkanContext* _context)
{
    context = _context;
}

void SubmitContextDepot::Shutdown()
{
    std::lock_guard lock(mutex);
    for (uint32_t i = 0; i < createdCount; i++) {
        vkDestroyFence(context->device, contexts[i].fence, nullptr);
        vkDestroyCommandPool(context->device, contexts[i].pool, nullptr);
        contexts[i] = {};
    }
    createdCount = 0;
    freelist.Clear();
}

SubmitContext* SubmitContextDepot::CheckOut(uint32_t queueFamily)
{
    std::lock_guard lock(mutex);
    for (size_t i = 0; i < freelist.Size(); i++) {
        if (freelist[i]->queueFamily == queueFamily) {
            SubmitContext* submitContext = freelist[i];
            freelist[i] = freelist[freelist.Size() - 1];
            freelist.PopBack();
            return submitContext;
        }
    }

    assert(createdCount < SUBMIT_CONTEXT_DEPOT_MAX && "SubmitContextDepot exhausted; cap must cover all GPU-uploading slots");
    ZoneScopedN("SubmitContextDepot Create Pool/Fence");
    SubmitContext& submitContext = contexts[createdCount];
    createdCount++;

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(queueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &submitContext.pool));

    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, submitContext.pool);
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, &submitContext.cmd));

    VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(context->device, &fenceInfo, nullptr, &submitContext.fence));

    submitContext.queueFamily = queueFamily;
    return &submitContext;
}

void SubmitContextDepot::Return(SubmitContext* submitContext)
{
    VK_CHECK(vkResetCommandPool(context->device, submitContext->pool, 0));
    VK_CHECK(vkResetFences(context->device, 1, &submitContext->fence));
    std::lock_guard lock(mutex);
    freelist.PushBack(submitContext);
}
} // AssetLoad
