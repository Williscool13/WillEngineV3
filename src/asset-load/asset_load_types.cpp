//
// Created by William on 2025-12-18.
//

#include "asset_load_types.h"

#include "render/model/model_serialization.h"
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
    stagingBuffer = std::move(Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, stagingSize));
    stagingAllocator = Core::LinearAllocator{stagingSize};
}
} // AssetLoad
