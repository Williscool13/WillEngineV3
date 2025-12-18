//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_TYPES_H
#define WILL_ENGINE_ASSET_LOAD_TYPES_H
#include <atomic>
#include <chrono>
#include <filesystem>

#include "offsetAllocator.hpp"
#include "will_model_asset.h"
#include "core/allocators/handle.h"
#include "render/model/model_format.h"

namespace AssetLoad
{
constexpr uint32_t ASSET_LOAD_STAGING_BUFFER_SIZE = 2 * 64 * 1024 * 1024; // 2MB

struct UploadStaging
{
    VkCommandBuffer commandBuffer{};
    VkFence fence{};

    Render::AllocatedBuffer stagingBuffer{};
    OffsetAllocator::Allocator stagingAllocator{ASSET_LOAD_STAGING_BUFFER_SIZE};
};

using UploadStagingHandle = Core::Handle<UploadStaging>;

using WillModelHandle = Core::Handle<WillModelAsset>;

struct AssetLoadRequest
{
    std::filesystem::path path;
    std::function<void(WillModelHandle)> onComplete;
};

struct AssetLoadInProgress
{
    WillModelHandle modelEntryHandle;
    std::function<void(WillModelHandle)> onComplete;
};

struct AssetLoadComplete
{
    WillModelHandle handle;
    std::function<void(WillModelHandle)> onComplete;
};
} // AssetLoad

#endif //WILL_ENGINE_ASSET_LOAD_TYPES_H