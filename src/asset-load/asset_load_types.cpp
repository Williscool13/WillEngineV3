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
    bStagingExists = true;
}

void UploadStaging::Destroy()
{
    stagingBuffer = {};
    stagingAllocator = Core::LinearAllocator{1, "AssetUploadStaging"};
    bStagingExists = false;
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
    Evictions evictions{};
    {
        std::lock_guard lock(mutex);
        if (freeSlots.IsEmpty()) { return nullptr; }

        // Greedy best fit.
        size_t pick = freeSlots.Size();
        uint64_t pickCapacity = 0;
        for (size_t i = 0; i < freeSlots.Size(); ++i) {
            const uint64_t capacity = freeSlots[i]->GetCapacity();
            if (capacity < grant) { continue; }
            if (pick == freeSlots.Size() || capacity < pickCapacity) {
                pick = i;
                pickCapacity = capacity;
            }
        }

        if (pick != freeSlots.Size()) {
            staging = freeSlots[pick];
            freeSlots.RemoveAt(pick);
            staging->GetStagingAllocator().Reset();
            return staging;
        }

        // Nothing resident is large enough.
        for (size_t i = 0; i < freeSlots.Size(); ++i) {
            const uint64_t capacity = freeSlots[i]->GetCapacity();
            if (pick == freeSlots.Size() || capacity > pickCapacity) {
                pick = i;
                pickCapacity = capacity;
            }
        }

        uint64_t projected = residentBytes - pickCapacity + grant;
        for (size_t i = 0; i < freeSlots.Size() && projected > budgetBytes; ++i) {
            if (i == pick) { continue; }
            const uint64_t capacity = freeSlots[i]->GetCapacity();
            if (capacity == 0) { continue; }
            projected -= capacity;
        }
        if (projected > budgetBytes) { return nullptr; }

        staging = freeSlots[pick];
        freeSlots.RemoveAt(pick);
        residentBytes -= pickCapacity;
        CollectEvictions(budgetBytes - grant, evictions);
        residentBytes += grant;
    }

    ZoneScopedN("UploadStagingDepot Grow Staging Buffer");
    // Release before allocating so the resident cap is never transiently exceeded
    DestroyAndRestore(evictions);
    staging->Destroy();
    staging->Initialize(context, grant);
    return staging;
}

void UploadStagingDepot::Return(UploadStaging* staging)
{
    std::lock_guard lock(mutex);
    freeSlots.PushBack(staging);
}

void UploadStagingDepot::CollectEvictions(uint64_t targetBytes, Evictions& out)
{
    for (size_t i = 0; i < freeSlots.Size() && residentBytes > targetBytes;) {
        const uint64_t capacity = freeSlots[i]->GetCapacity();
        if (capacity == 0) {
            ++i;
            continue;
        }
        residentBytes -= capacity;
        out.PushBack(freeSlots[i]);
        freeSlots.RemoveAt(i);
    }
}

void UploadStagingDepot::DestroyAndRestore(const Evictions& evictions)
{
    if (evictions.IsEmpty()) { return; }
    {
        ZoneScopedN("UploadStagingDepot Destroy Staging Buffer");
        for (UploadStaging* victim : evictions) { victim->Destroy(); }
    }
    std::lock_guard lock(mutex);
    for (UploadStaging* victim : evictions) { freeSlots.PushBack(victim); }
}

bool UploadStagingDepot::CanTrim(uint64_t targetBytes) const
{
    std::lock_guard lock(mutex);
    return freeSlots.Size() == UPLOAD_STAGING_DEPOT_MAX && residentBytes > targetBytes;
}

void UploadStagingDepot::TrimIdle(uint64_t targetBytes)
{
    Evictions evictions{};
    {
        std::lock_guard lock(mutex);
        if (freeSlots.Size() != UPLOAD_STAGING_DEPOT_MAX) { return; }
        CollectEvictions(targetBytes, evictions);
    }
    DestroyAndRestore(evictions);
}

void UploadStagingDepot::SetBudgetBytes(uint64_t newBudget)
{
    assert((newBudget == UPLOAD_STAGING_BUDGET_DEFAULT || newBudget == UPLOAD_STAGING_BUDGET_LOADING_SCREEN) && "Budget must be a config preset");
    Evictions evictions{};
    {
        std::lock_guard lock(mutex);
        budgetBytes = newBudget;
        CollectEvictions(newBudget, evictions);
    }
    DestroyAndRestore(evictions);
}

void SubmitContext::Initialize(Render::VulkanContext* context, uint32_t queueFamily)
{
    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(queueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, context->HostAllocCallbacks(), &pool));

    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, pool);
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, &cmd));

    VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(context->device, &fenceInfo, context->HostAllocCallbacks(), &fence));
}

void SubmitContext::Reset(Render::VulkanContext* context)
{
    VK_CHECK(vkResetCommandPool(context->device, pool, 0));
    VK_CHECK(vkResetFences(context->device, 1, &fence));
}

void SubmitContext::Destroy(Render::VulkanContext* context)
{
    if (pool == VK_NULL_HANDLE) { return; }
    vkDestroyFence(context->device, fence, context->HostAllocCallbacks());
    vkDestroyCommandPool(context->device, pool, context->HostAllocCallbacks());
    pool = VK_NULL_HANDLE;
    cmd = VK_NULL_HANDLE;
    fence = VK_NULL_HANDLE;
}
} // AssetLoad
