//
// Created by William on 2025-12-18.
//

#include "asset_load_types.h"

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
    stagingBuffer = std::move(Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, stagingSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR));
    stagingBuffer.address = Render::VkHelpers::GetDeviceAddress(context->device, stagingBuffer.handle);
    stagingAllocator = Core::LinearAllocator{stagingSize, "AssetUploadStaging"};
}
} // AssetLoad
