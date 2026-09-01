//
// Created by William on 2025-10-10.
//

#include "vk_synchronization.h"

#include "vk_context.h"
#include "vk_helpers.h"
#include "vk_utils.h"

namespace Render
{
RenderSynchronization::RenderSynchronization(VulkanContext* context)
    : context(context)
{}

RenderSynchronization::~RenderSynchronization()
{
    if (context && commandPool != VK_NULL_HANDLE) {
        // Command buffer is freed when pool is destroyed.
        vkDestroyCommandPool(context->device, commandPool, context->HostAllocCallbacks());
        vkDestroyFence(context->device, renderFence, context->HostAllocCallbacks());
        vkDestroySemaphore(context->device, swapchainSemaphore, context->HostAllocCallbacks());
        vkDestroySemaphore(context->device, renderSemaphore, context->HostAllocCallbacks());
    }
}

RenderSynchronization::RenderSynchronization(RenderSynchronization&& other) noexcept
{
    context = other.context;
    commandPool = other.commandPool;
    commandBuffer = other.commandBuffer;
    asyncComputeCommandBuffer = other.asyncComputeCommandBuffer;
    renderFence = other.renderFence;
    swapchainSemaphore = other.swapchainSemaphore;
    renderSemaphore = other.renderSemaphore;

    other.context = nullptr;
    other.commandPool = VK_NULL_HANDLE;
    other.commandBuffer = VK_NULL_HANDLE;
    other.asyncComputeCommandBuffer = VK_NULL_HANDLE;
    other.renderFence = VK_NULL_HANDLE;
    other.swapchainSemaphore = VK_NULL_HANDLE;
    other.renderSemaphore = VK_NULL_HANDLE;
}

RenderSynchronization& RenderSynchronization::operator=(RenderSynchronization&& other) noexcept
{
    if (this != &other) {
        if (context && commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(context->device, commandPool, context->HostAllocCallbacks());
            vkDestroyFence(context->device, renderFence, context->HostAllocCallbacks());
            vkDestroySemaphore(context->device, swapchainSemaphore, context->HostAllocCallbacks());
            vkDestroySemaphore(context->device, renderSemaphore, context->HostAllocCallbacks());
        }

        context = other.context;
        commandPool = other.commandPool;
        commandBuffer = other.commandBuffer;
        asyncComputeCommandBuffer = other.asyncComputeCommandBuffer;
        renderFence = other.renderFence;
        swapchainSemaphore = other.swapchainSemaphore;
        renderSemaphore = other.renderSemaphore;

        other.context = nullptr;
        other.commandPool = VK_NULL_HANDLE;
        other.commandBuffer = VK_NULL_HANDLE;
        other.asyncComputeCommandBuffer = VK_NULL_HANDLE;
        other.renderFence = VK_NULL_HANDLE;
        other.swapchainSemaphore = VK_NULL_HANDLE;
        other.renderSemaphore = VK_NULL_HANDLE;
    }
    return *this;
}

void RenderSynchronization::Initialize()
{
    VkCommandPoolCreateInfo commandPoolCreateInfo = VkHelpers::CommandPoolCreateInfo(context->graphicsQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &commandPoolCreateInfo, context->HostAllocCallbacks(), &commandPool));
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = VkHelpers::CommandBufferAllocateInfo(2, commandPool);
    VkCommandBuffer commandBuffers[2]{};
    VK_CHECK(vkAllocateCommandBuffers(context->device, &commandBufferAllocateInfo, commandBuffers));
    commandBuffer = commandBuffers[0];
    asyncComputeCommandBuffer = commandBuffers[1];

    const VkFenceCreateInfo fenceCreateInfo = VkHelpers::FenceCreateInfo();
    const VkSemaphoreCreateInfo semaphoreCreateInfo = VkHelpers::SemaphoreCreateInfo();
    VK_CHECK(vkCreateFence(context->device, &fenceCreateInfo, context->HostAllocCallbacks(), &renderFence));
    VK_CHECK(vkCreateSemaphore(context->device, &semaphoreCreateInfo, context->HostAllocCallbacks(), &swapchainSemaphore));
    VK_CHECK(vkCreateSemaphore(context->device, &semaphoreCreateInfo, context->HostAllocCallbacks(), &renderSemaphore));
}

void RenderSynchronization::RecreateSynchronization()
{
    vkDestroyFence(context->device, renderFence, context->HostAllocCallbacks());
    vkDestroySemaphore(context->device, swapchainSemaphore, context->HostAllocCallbacks());
    vkDestroySemaphore(context->device, renderSemaphore, context->HostAllocCallbacks());

    const VkFenceCreateInfo fenceCreateInfo = VkHelpers::FenceCreateInfo();
    const VkSemaphoreCreateInfo semaphoreCreateInfo = VkHelpers::SemaphoreCreateInfo();
    VK_CHECK(vkCreateFence(context->device, &fenceCreateInfo, context->HostAllocCallbacks(), &renderFence));
    VK_CHECK(vkCreateSemaphore(context->device, &semaphoreCreateInfo, context->HostAllocCallbacks(), &swapchainSemaphore));
    VK_CHECK(vkCreateSemaphore(context->device, &semaphoreCreateInfo, context->HostAllocCallbacks(), &renderSemaphore));
}
} // Renderer
