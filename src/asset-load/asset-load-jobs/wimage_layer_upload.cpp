//
// Created by William on 2026-10-08.
//

#include "wimage_layer_upload.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "asset-load/asset_load_types.h"
#include "engine/resources/wimage_format.h"
#include "render/vulkan/vk_helpers.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
void UploadWImageLayers(VkCommandBuffer cmd, const Engine::WImageView& blobView, VkImage image, UploadStaging* uploadStaging, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadWImageLayers");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();
    const VkImageSubresourceRange range = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, blobView.levelCount, 0, blobView.faceCount);

    VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
        image, range,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &preCopyBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    for (uint32_t mipLevel = 0; mipLevel < blobView.levelCount; mipLevel++) {
        const uint32_t mipWidth = blobView.LevelWidth(mipLevel);
        const uint32_t mipHeight = blobView.LevelHeight(mipLevel);
        const size_t mipSize = blobView.FaceSize(mipLevel);
        const size_t rowPitch = blobView.RowPitch(mipLevel);
        const uint32_t totalRows = std::max(1u, static_cast<uint32_t>(mipSize / rowPitch));
        const uint32_t texelRowsPerRow = (mipHeight + totalRows - 1) / totalRows;

        for (uint32_t face = 0; face < blobView.faceCount; face++) {
            ZoneScopedN("Upload Face");

            const uint8_t* faceData = blobView.FaceData(mipLevel, face);

            uint32_t rowsDone = 0;
            while (rowsDone < totalRows) {
                uint32_t rowsFit = static_cast<uint32_t>(stagingAllocator.GetRemaining() / rowPitch);
                if (rowsFit == 0) {
                    submitAndWait(true);
                    stagingAllocator.Reset();
                    rowsFit = static_cast<uint32_t>(stagingAllocator.GetRemaining() / rowPitch);
                    assert(rowsFit > 0 && "Single block row too large for staging buffer");
                }
                rowsFit = std::min(rowsFit, totalRows - rowsDone);
                const size_t chunkBytes = static_cast<size_t>(rowsFit) * rowPitch;
                const size_t allocation = stagingAllocator.Allocate(chunkBytes, 16);
                if (allocation == SIZE_MAX) {
                    submitAndWait(true);
                    stagingAllocator.Reset();
                    continue;
                }

                char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
                memcpy(stagingPtr, faceData + static_cast<size_t>(rowsDone) * rowPitch, chunkBytes);

                const uint32_t texelY = rowsDone * texelRowsPerRow;
                VkBufferImageCopy copyRegion{};
                copyRegion.bufferOffset = allocation;
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = mipLevel;
                copyRegion.imageSubresource.baseArrayLayer = face;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageOffset = {0, static_cast<int32_t>(texelY), 0};
                copyRegion.imageExtent = {mipWidth, std::min(rowsFit * texelRowsPerRow, mipHeight - texelY), 1};

                vkCmdCopyBufferToImage(cmd, stagingBuffer.handle, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

                rowsDone += rowsFit;
            }
        }
    }

    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        image, range,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    depInfo.pImageMemoryBarriers = &finalBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
}
} // AssetLoad
