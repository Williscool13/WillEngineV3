//
// Created by William on 2025-12-27.
//

#include "render_graph.h"

#include <cassert>
#include <utility>
#include <unordered_set>

#include "render_graph_config.h"
#include "render_pass.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
RenderGraph::RenderGraph(VulkanContext* context, ResourceManager* resourceManager)
    : context(context), resourceManager(resourceManager)
{
    textures.reserve(RDG_MAX_SAMPLED_TEXTURES);
    physicalResources.reserve(256);
    textures.reserve(256);
    buffers.reserve(256);

    for (int32_t i = 0; i < uploadArenas.size(); ++i) {
        VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.pNext = nullptr;
        bufferInfo.usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo vmaAllocInfo = {};
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        bufferInfo.size = RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE;

        uploadArenas[i].buffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo));
        uploadArenas[i].buffer.SetDebugName(("frameBufferUploader_" + std::to_string(i)).c_str());
        uploadArenas[i].allocator = Core::LinearAllocator(RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE);
        uploadArenas[i].size = RDG_DEFAULT_UPLOAD_LINEAR_ALLOCATOR_SIZE;
    }
}

RenderGraph::~RenderGraph()
{
    for (auto& phys : physicalResources) {
        DestroyPhysicalResource(phys);
    }
}

RenderPass& RenderGraph::AddPass(const std::string& name, VkPipelineStageFlags2 stages)
{
    passes.push_back(std::make_unique<RenderPass>(*this, name, stages));
    return *passes.back();
}

void RenderGraph::PrunePasses()
{
    // Add pruning when productive pruning is actually relevant
}

void RenderGraph::AccumulateTextureUsage()
{
    for (auto& pass : passes) {
        for (const uint32_t texIndex : pass->storageImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        for (const uint32_t texIndex : pass->storageImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        for (const uint32_t texIndex : pass->sampledImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        for (const uint32_t texIndex : pass->imageReadWrite) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        for (const uint32_t texIndex : pass->clearImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (const uint32_t texIndex : pass->blitImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (const uint32_t texIndex : pass->blitImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (const uint32_t texIndex : pass->copyImageReads) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (const uint32_t texIndex : pass->copyImageWrites) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (const uint32_t texIndex : pass->colorAttachments) {
            auto& tex = textures[texIndex];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }

        if (pass->depthStencilAttachment != UINT_MAX) {
            auto& tex = textures[pass->depthStencilAttachment];
            tex.accumulatedUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
    }
}

void RenderGraph::CalculateLifetimes()
{
    for (uint32_t passIdx = 0; passIdx < passes.size(); passIdx++) {
        auto& pass = passes[passIdx];

        auto UpdateTextureLifetime = [passIdx](TextureResource& tex) {
            tex.firstPass = std::min(tex.firstPass, passIdx);
            tex.lastPass = std::max(tex.lastPass, passIdx);
        };

        auto UpdateBufferLifetime = [passIdx](BufferResource& buf) {
            buf.firstPass = std::min(buf.firstPass, passIdx);
            buf.lastPass = std::max(buf.lastPass, passIdx);
        };

        for (const uint32_t texIndex : pass->storageImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->storageImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->sampledImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->imageReadWrite) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->clearImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->blitImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->blitImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->copyImageReads) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->copyImageWrites) { UpdateTextureLifetime(textures[texIndex]); }
        for (const uint32_t texIndex : pass->colorAttachments) { UpdateTextureLifetime(textures[texIndex]); }
        if (pass->depthStencilAttachment != UINT_MAX) { UpdateTextureLifetime(textures[pass->depthStencilAttachment]); }

        for (const uint32_t bufIndex : pass->bufferReads) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferWrites) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferReadWrite) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferReadTransfer) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferWriteTransfer) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferIndirectReads) { UpdateBufferLifetime(buffers[bufIndex]); }
        for (const uint32_t bufIndex : pass->bufferIndirectCountReads) { UpdateBufferLifetime(buffers[bufIndex]); }
    }
}

void RenderGraph::Compile(int64_t currentFrame)
{
    PrunePasses();

    AccumulateTextureUsage();

    CalculateLifetimes();

    for (auto& tex : textures) {
        if (!tex.HasPhysical()) {
            // Build desired dimensions for this texture
            ResourceDimensions desiredDim;
            desiredDim.type = ResourceDimensions::Type::Image;
            desiredDim.format = tex.textureInfo.format;
            desiredDim.width = tex.textureInfo.width;
            desiredDim.height = tex.textureInfo.height;
            desiredDim.depth = 1;
            desiredDim.levels = tex.textureInfo.mipLevels;
            desiredDim.layers = 1;
            desiredDim.samples = 1;
            desiredDim.imageUsage = tex.accumulatedUsage;
            desiredDim.name = tex.name;

            // Try to find existing physical resource with matching dimensions
            bool foundAlias = false;
            for (uint32_t i = 0; i < physicalResources.size(); i++) {
                auto& phys = physicalResources[i];
                if (phys.bIsImported) { continue; }
                if (!phys.bCanAlias) { continue; }
                if (phys.dimensions != desiredDim) { continue; }

                // Cross-frame resources can't alias at all.
                // Well, not strictly true. If a texture is carried over to next frame, it can be aliased if the other use is before the texture's first pass.
                if (!tex.bCanUseAliasedTexture && !phys.logicalResourceIndices.empty()) {
                    continue;
                }

                // If reusing already existing allocated resource, must be superset
                if (phys.IsAllocated()) {
                    if ((phys.dimensions.imageUsage & tex.accumulatedUsage) != tex.accumulatedUsage) {
                        continue;
                    }
                }

                bool canAlias = true;
                for (uint32_t logicalIdx : phys.logicalResourceIndices) {
                    auto& existing = textures[logicalIdx];

                    bool overlap = !(tex.lastPass < existing.firstPass || existing.lastPass < tex.firstPass);

                    if (overlap) {
                        canAlias = false;
                        break;
                    }
                }

                if (canAlias) {
                    tex.physicalIndex = i;
                    if (!phys.IsAllocated()) {
                        phys.dimensions.imageUsage |= tex.accumulatedUsage;
                    }
                    phys.logicalResourceIndices.push_back(tex.index);
                    phys.bCanAlias = tex.bCanUseAliasedTexture;
                    AppendUsageChain(phys, tex.name, tex.bCanUseAliasedTexture, bDebugLogging);
                    foundAlias = true;
                    break;
                }
            }

            if (!foundAlias) {
                // No alias found, allocate new physical resource
                tex.physicalIndex = physicalResources.size();
                physicalResources.emplace_back();
                auto& newPhys = physicalResources.back();
                newPhys.dimensions = desiredDim;
                newPhys.logicalResourceIndices.push_back(tex.index);
                newPhys.bCanAlias = tex.bCanUseAliasedTexture;
                AppendUsageChain(newPhys, tex.name, tex.bCanUseAliasedTexture, bDebugLogging);
            }
        }
    }

    for (auto& tex : textures) {
        auto& phys = physicalResources[tex.physicalIndex];
        if (!phys.IsAllocated() && tex.textureInfo.format != VK_FORMAT_UNDEFINED) {
            CreatePhysicalImage(phys, phys.dimensions);
        }
        phys.lastUsedFrame = currentFrame;
    }

    for (auto& buf : buffers) {
        if (buf.accumulatedUsage == 0) {
            if (bDebugLogging) {
                SPDLOG_WARN("Buffer '{}' created but never used", buf.name);
            }
            continue;
        }

        if (!buf.HasPhysical()) {
            ResourceDimensions desiredDim;
            desiredDim.type = ResourceDimensions::Type::Buffer;
            desiredDim.bufferSize = buf.bufferInfo.size;
            desiredDim.bufferUsage = buf.accumulatedUsage;
            desiredDim.name = buf.name;

            bool foundAlias = false;
            for (uint32_t i = 0; i < physicalResources.size(); i++) {
                auto& phys = physicalResources[i];

                if (phys.bIsImported) { continue; }
                if (!phys.bCanAlias) { continue; }
                if (!phys.dimensions.IsBuffer()) { continue; }

                if (phys.dimensions.bufferSize != desiredDim.bufferSize) { continue; }

                // Cross-frame resources can't alias at all.
                if (!buf.bCanUseAliasedBuffer && !phys.logicalResourceIndices.empty()) {
                    continue;
                }

                // If already allocated, must be superset
                if (phys.IsAllocated()) {
                    if ((phys.dimensions.bufferUsage & buf.accumulatedUsage) != buf.accumulatedUsage) {
                        continue;
                    }
                }

                bool canAlias = true;
                for (uint32_t logicalIdx : phys.logicalResourceIndices) {
                    auto& existing = buffers[logicalIdx];
                    if (!(buf.lastPass < existing.firstPass || existing.lastPass < buf.firstPass)) {
                        canAlias = false;
                        break;
                    }
                }

                if (canAlias) {
                    buf.physicalIndex = i;
                    phys.logicalResourceIndices.push_back(buf.index);
                    if (!phys.IsAllocated()) {
                        phys.dimensions.bufferUsage |= buf.accumulatedUsage;
                    }
                    phys.bCanAlias = buf.bCanUseAliasedBuffer;
                    AppendUsageChain(phys, buf.name, buf.bCanUseAliasedBuffer, bDebugLogging);
                    foundAlias = true;
                    break;
                }
            }

            if (!foundAlias) {
                buf.physicalIndex = physicalResources.size();
                physicalResources.emplace_back();
                auto& newPhys = physicalResources.back();
                newPhys.dimensions = desiredDim;
                newPhys.logicalResourceIndices.push_back(buf.index);
                newPhys.bCanAlias = buf.bCanUseAliasedBuffer;
                AppendUsageChain(newPhys, buf.name, buf.bCanUseAliasedBuffer, bDebugLogging);
            }
        }
    }

    for (auto& buf : buffers) {
        if (buf.accumulatedUsage == 0) { continue; }

        auto& phys = physicalResources[buf.physicalIndex];
        if (!phys.IsAllocated() && buf.bufferInfo.size > 0) {
            CreatePhysicalBuffer(phys, phys.dimensions);
        }
        phys.lastUsedFrame = currentFrame;
    }

    for (auto& phys : physicalResources) {
        if (phys.NeedsDescriptorWrite() && phys.imageView != VK_NULL_HANDLE) {
            if (phys.NeedsDescriptorWrite() && phys.imageView != VK_NULL_HANDLE) {
                phys.sampledDescriptorHandle = transientSampledImageHandleAllocator.Add();
                assert(phys.sampledDescriptorHandle.IsValid());
                resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledImageDescriptor(
                    phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
                );

                if (phys.depthOnlyView != VK_NULL_HANDLE) {
                    phys.depthOnlyDescriptorHandle = transientSampledImageHandleAllocator.Add();
                    assert(phys.depthOnlyDescriptorHandle.IsValid());
                    resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledImageDescriptor(
                        phys.depthOnlyDescriptorHandle.index, {nullptr, phys.depthOnlyView, VK_IMAGE_LAYOUT_GENERAL}
                    );
                }

                if (phys.stencilOnlyView != VK_NULL_HANDLE) {
                    phys.stencilOnlyDescriptorHandle = transientStorageUIntHandleAllocator.Add();
                    assert(phys.stencilOnlyDescriptorHandle.IsValid());
                    resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageUIntDescriptor(
                        phys.stencilOnlyDescriptorHandle.index,
                        {nullptr, phys.stencilOnlyView, VK_IMAGE_LAYOUT_GENERAL}
                    );
                }

                StorageImageType storageType = GetStorageImageType(phys.dimensions.format, phys.aspect);
                for (uint32_t mip = 0; mip < phys.dimensions.levels; ++mip) {
                    switch (storageType) {
                        case StorageImageType::Float4:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageFloat4HandleAllocator.Add();
                            assert(phys.storageMipDescriptorHandles[mip].IsValid());
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageFloat4Descriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case StorageImageType::Float2:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageFloat2HandleAllocator.Add();
                            assert(phys.storageMipDescriptorHandles[mip].IsValid());
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageFloat2Descriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case StorageImageType::Float:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageFloatHandleAllocator.Add();
                            assert(phys.storageMipDescriptorHandles[mip].IsValid());
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageFloatDescriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case StorageImageType::UInt4:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageUInt4HandleAllocator.Add();
                            assert(phys.storageMipDescriptorHandles[mip].IsValid());
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageUIntDescriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                        case StorageImageType::UInt:
                        {
                            phys.storageMipDescriptorHandles[mip] = transientStorageUIntHandleAllocator.Add();
                            assert(phys.storageMipDescriptorHandles[mip].IsValid());
                            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageUIntDescriptor(
                                phys.storageMipDescriptorHandles[mip].index,
                                {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                            );
                            break;
                        }
                    }
                }

                phys.descriptorWritten = true;
            }
        }

        if (phys.NeedsAddressRetrieval()) {
            VkBufferDeviceAddressInfo info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            info.buffer = phys.buffer;
            phys.bufferAddress = vkGetBufferDeviceAddress(context->device, &info);
            phys.addressRetrieved = true;
        }
    }

    if (bDebugLogging) {
        SPDLOG_INFO("=== Physical Resource Aliasing Chains ===");
        for (size_t i = 0; i < physicalResources.size(); ++i) {
            const auto& phys = physicalResources[i];
            if (!phys.usageChain.empty()) {
                SPDLOG_INFO("  Phys[{}]: {}", i, phys.usageChain);
            }
        }
    }
}

void RenderGraph::Execute(VkCommandBuffer cmd)
{
    if (bDebugLogging) {
        SPDLOG_INFO("=== RenderGraph Execution ===");
    }

    for (auto& pass : passes) {
        if (bDebugLogging) {
            SPDLOG_INFO("[PASS] {}", pass->renderPassName);
        }
        std::vector<VkImageMemoryBarrier2> barriers;

        auto GetPhysical = [this](uint32_t texIndex) -> PhysicalResource& {
            return physicalResources[textures[texIndex].physicalIndex];
        };

        for (const uint32_t texIndex : pass->colorAttachments) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        if (pass->depthStencilAttachment != UINT_MAX) {
            const uint32_t texIndex = pass->depthStencilAttachment;
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);

            VkPipelineStageFlags2 dstStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            VkAccessFlags2 dstAccess = 0;
            if ((pass->depthAccessType & DepthAccessType::Read) != DepthAccessType::None) {
                dstAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            }
            if ((pass->depthAccessType & DepthAccessType::Write) != DepthAccessType::None) {
                dstAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout, dstStages, dstAccess, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->storageImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                pass->stages, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->storageImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                pass->stages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->sampledImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                pass->stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->imageReadWrite) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                pass->stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->blitImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->clearImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->blitImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->copyImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        for (const uint32_t texIndex : pass->copyImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex.layout,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            LogImageBarrier(barrier, tex.name, tex.physicalIndex);
            barriers.push_back(barrier);
        }

        std::vector<VkBufferMemoryBarrier2> bufferBarriers;

        for (const uint32_t bufIndex : pass->bufferWrites) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            if (phys.bDisableBarriers) { continue; }

            VkAccessFlags2 desiredAccess = VK_ACCESS_2_SHADER_WRITE_BIT;

            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = phys.event.stages,
                .srcAccessMask = phys.event.access,
                .dstStageMask = pass->stages,
                .dstAccessMask = desiredAccess,
                .buffer = phys.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            bufferBarriers.push_back(barrier);
            LogBufferBarrier(buf.name, desiredAccess);
        }

        for (const uint32_t bufIndex : pass->bufferReadWrite) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            if (phys.bDisableBarriers) { continue; }

            VkAccessFlags2 desiredAccess = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;

            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = phys.event.stages,
                .srcAccessMask = phys.event.access,
                .dstStageMask = pass->stages,
                .dstAccessMask = desiredAccess,
                .buffer = phys.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            bufferBarriers.push_back(barrier);
            LogBufferBarrier(buf.name, desiredAccess);
        }

        for (const uint32_t bufIndex : pass->bufferWriteTransfer) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            if (phys.bDisableBarriers) { continue; }

            VkAccessFlags2 desiredAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;

            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = phys.event.stages,
                .srcAccessMask = phys.event.access,
                .dstStageMask = pass->stages,
                .dstAccessMask = desiredAccess,
                .buffer = phys.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            bufferBarriers.push_back(barrier);
            LogBufferBarrier(buf.name, desiredAccess);
        }

        for (const uint32_t bufIndex : pass->bufferReadTransfer) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            if (phys.bDisableBarriers) { continue; }

            VkAccessFlags2 desiredAccess = VK_ACCESS_2_TRANSFER_READ_BIT;

            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = phys.event.stages,
                .srcAccessMask = phys.event.access,
                .dstStageMask = pass->stages,
                .dstAccessMask = desiredAccess,
                .buffer = phys.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            bufferBarriers.push_back(barrier);
            LogBufferBarrier(buf.name, desiredAccess);
        }

        for (const uint32_t bufIndex : pass->bufferReads) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            if (phys.bDisableBarriers) { continue; }

            VkAccessFlags2 desiredAccess = VK_ACCESS_2_SHADER_READ_BIT;
            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = phys.event.stages,
                .srcAccessMask = phys.event.access,
                .dstStageMask = pass->stages,
                .dstAccessMask = desiredAccess,
                .buffer = phys.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            bufferBarriers.push_back(barrier);
            LogBufferBarrier(buf.name, desiredAccess);
        }

        for (const uint32_t bufIndex : pass->bufferIndirectReads) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            if (phys.bDisableBarriers) { continue; }

            VkAccessFlags2 desiredAccess = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = phys.event.stages,
                .srcAccessMask = phys.event.access,
                .dstStageMask = pass->stages | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                .dstAccessMask = desiredAccess,
                .buffer = phys.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            bufferBarriers.push_back(barrier);
            LogBufferBarrier(buf.name, desiredAccess);
        }

        for (const uint32_t bufIndex : pass->bufferIndirectCountReads) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            if (phys.bDisableBarriers) { continue; }

            VkAccessFlags2 desiredAccess = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = phys.event.stages,
                .srcAccessMask = phys.event.access,
                .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                .dstAccessMask = desiredAccess,
                .buffer = phys.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };
            bufferBarriers.push_back(barrier);
            LogBufferBarrier(buf.name, desiredAccess);
        }

        if (!barriers.empty() || !bufferBarriers.empty()) {
            if (bDebugLogging) {
                if (!barriers.empty() && !bufferBarriers.empty()) {
                    SPDLOG_INFO("  Inserting {} image, {} buffer barrier(s)", barriers.size(), bufferBarriers.size());
                }
                else if (!barriers.empty()) {
                    SPDLOG_INFO("  Inserting {} image barrier(s)", barriers.size());
                }
                else {
                    SPDLOG_INFO("  Inserting {} buffer barrier(s)", bufferBarriers.size());
                }
            }
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            depInfo.pImageMemoryBarriers = barriers.data();
            depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
            depInfo.pBufferMemoryBarriers = bufferBarriers.data();
            vkCmdPipelineBarrier2(cmd, &depInfo);
        }

        // Execute pass
        if (pass->executeFunc) {
            VkDebugUtilsLabelEXT label = {};
            label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = pass->renderPassName.c_str();
            vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
            pass->executeFunc(cmd);
            vkCmdEndDebugUtilsLabelEXT(cmd);
        }

        for (const uint32_t texIndex : pass->colorAttachments) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            phys.event.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if (pass->depthStencilAttachment != UINT_MAX) {
            VkAccessFlags2 dstAccess = 0;
            if ((pass->depthAccessType & DepthAccessType::Read) != DepthAccessType::None) {
                dstAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            }
            if ((pass->depthAccessType & DepthAccessType::Write) != DepthAccessType::None) {
                dstAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            assert(dstAccess != 0 && "Depth/stencil attachment must have at least Read or Write access");

            const uint32_t texIndex = pass->depthStencilAttachment;
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            phys.event.access = dstAccess;
        }

        for (const uint32_t texIndex : pass->storageImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_GENERAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }

        for (const uint32_t texIndex : pass->storageImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_GENERAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }

        for (const uint32_t texIndex : pass->sampledImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }

        for (const uint32_t texIndex : pass->imageReadWrite) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_GENERAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }

        for (const uint32_t texIndex : pass->clearImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_CLEAR_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        for (const uint32_t texIndex : pass->blitImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_BLIT_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        for (const uint32_t texIndex : pass->blitImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_BLIT_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        for (const uint32_t texIndex : pass->copyImageWrites) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        for (const uint32_t texIndex : pass->copyImageReads) {
            auto& tex = textures[texIndex];
            auto& phys = GetPhysical(texIndex);
            tex.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferReads) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_READ_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferWrites) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_WRITE_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferReadWrite) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferWriteTransfer) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferReadTransfer) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferIndirectReads) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            phys.event.stages = pass->stages | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            phys.event.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        }

        for (const uint32_t bufIndex : pass->bufferIndirectCountReads) {
            auto& buf = buffers[bufIndex];
            auto& phys = physicalResources[buf.physicalIndex];
            phys.event.stages = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            phys.event.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }
    }

    // Final barriers for imported resources
    if (bDebugLogging) {
        SPDLOG_INFO("[FINAL BARRIERS]");
    }
    std::vector<VkImageMemoryBarrier2> finalBarriers;
    for (auto& tex : textures) {
        if (tex.HasPhysical() && tex.HasFinalLayout()) {
            auto& phys = physicalResources[tex.physicalIndex];
            if (tex.layout != tex.finalLayout) {
                auto finalBarrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, tex.layout,
                    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE, tex.finalLayout
                );
                LogImageBarrier(finalBarrier, tex.name, tex.physicalIndex);
                finalBarriers.push_back(finalBarrier);
                tex.layout = tex.finalLayout;
            }
        }
    }

    if (!finalBarriers.empty()) {
        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(finalBarriers.size());
        depInfo.pImageMemoryBarriers = finalBarriers.data();
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }
}

void RenderGraph::PrepareSwapchain(VkCommandBuffer cmd, const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    if (it == textureNameToIndex.end()) {
        SPDLOG_ERROR("[RenderGraph::PrepareSwapchain] Prepare swapchain failed.");
        return;
    }

    TextureResource& swapchainTexture = textures[it->second];
    auto& phys = physicalResources[swapchainTexture.physicalIndex];

    VkImageMemoryBarrier2 presentBarrier = VkHelpers::ImageMemoryBarrier(
        phys.image,
        VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        phys.event.stages, phys.event.access, swapchainTexture.layout,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

    VkDependencyInfo depInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &presentBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void RenderGraph::Reset(uint32_t _currentFrameIndex, uint64_t currentFrame, uint64_t maxFramesUnused)
{
    currentFrameIndex = _currentFrameIndex;
    uploadArenas[currentFrameIndex].allocator.Reset();

    for (TextureFrameCarryover& carryover : textureCarryovers) {
        if (const TextureResource* tex = GetTexture(carryover.srcName)) {
            const PhysicalResource& phys = physicalResources[tex->physicalIndex];
            carryover.physicalImage = phys.image;
            carryover.textInfo = tex->textureInfo;
            carryover.layout = tex->layout;
            carryover.accumulatedUsage = tex->accumulatedUsage;
        }
    }
    for (BufferFrameCarryover& carryover : bufferCarryovers) {
        if (const BufferResource* buf = GetBuffer(carryover.srcName)) {
            const PhysicalResource& phys = physicalResources[buf->physicalIndex];
            carryover.buffer = phys.buffer;
            carryover.bufferInfo = buf->bufferInfo;
            carryover.accumulatedUsage = buf->accumulatedUsage;
        }
    }

    passes.clear();
    textures.clear();
    textureNameToIndex.clear();
    buffers.clear();
    bufferNameToIndex.clear();

    for (auto& phys : physicalResources) {
        phys.logicalResourceIndices.clear();
        phys.bCanAlias = true;
    }

    // ====== This is the only time it is safe to reorder physical resource indices ======
    for (int i = static_cast<int>(physicalResources.size()) - 1; i >= 0; --i) {
        auto& phys = physicalResources[i];

        if (phys.bIsImported) { continue; }
        if (!phys.IsAllocated()) { continue; }

        if (currentFrame - phys.lastUsedFrame > maxFramesUnused) {
            DestroyPhysicalResource(phys);
            physicalResources.erase(physicalResources.begin() + i);
        }
    }
    // ============================================================================ ======

    for (auto& carryover : textureCarryovers) {
        uint32_t physicalIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.size(); i++) {
            if (physicalResources[i].image == carryover.physicalImage) {
                physicalIndex = i;
                break;
            }
        }

        if (physicalIndex == UINT32_MAX) {
            SPDLOG_ERROR("Carryover texture '{}' physical resource not found", carryover.dstName);
            continue;
        }

        TextureResource* newTex = GetOrCreateTexture(carryover.dstName);
        newTex->textureInfo = carryover.textInfo;
        newTex->layout = carryover.layout;
        newTex->accumulatedUsage = carryover.accumulatedUsage;
        newTex->physicalIndex = physicalIndex;

        PhysicalResource& phys = physicalResources[physicalIndex];
        phys.logicalResourceIndices.push_back(newTex->index);
        phys.usageChain.clear();
        AppendUsageChain(phys, newTex->name, newTex->bCanUseAliasedTexture, bDebugLogging);
        phys.bCanAlias = false;
    }
    textureCarryovers.clear();

    for (auto& carryover : bufferCarryovers) {
        uint32_t physicalIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.size(); i++) {
            if (physicalResources[i].buffer == carryover.buffer) {
                physicalIndex = i;
                break;
            }
        }

        if (physicalIndex == UINT32_MAX) {
            SPDLOG_ERROR("Carryover buffer '{}' physical resource not found", carryover.dstName);
            continue;
        }

        BufferResource* newBuf = GetOrCreateBuffer(carryover.dstName);
        newBuf->bufferInfo = carryover.bufferInfo;
        newBuf->accumulatedUsage = carryover.accumulatedUsage;
        newBuf->physicalIndex = physicalIndex;

        PhysicalResource& phys = physicalResources[physicalIndex];
        phys.logicalResourceIndices.push_back(newBuf->index);
        phys.usageChain.clear();
        AppendUsageChain(phys, newBuf->name, newBuf->bCanUseAliasedBuffer, bDebugLogging);
        phys.bCanAlias = false;
    }
    bufferCarryovers.clear();
}

void RenderGraph::InvalidateAll()
{
    textures.clear();
    textureNameToIndex.clear();
    buffers.clear();
    bufferNameToIndex.clear();

    passes.clear();

    for (PhysicalResource& physicalResource : physicalResources) {
        DestroyPhysicalResource(physicalResource);
    }
    physicalResources.clear();
    transientSampledImageHandleAllocator.Clear();
    transientStorageFloat4HandleAllocator.Clear();
    transientStorageFloat2HandleAllocator.Clear();
    transientStorageFloatHandleAllocator.Clear();
    transientStorageUInt4HandleAllocator.Clear();
    transientStorageUIntHandleAllocator.Clear();
    textureCarryovers.clear();
    bufferCarryovers.clear();
}

void RenderGraph::CreateTexture(const std::string& name, const TextureInfo& texInfo)
{
    TextureResource* resource = GetOrCreateTexture(name);

    if (resource->textureInfo.format != VK_FORMAT_UNDEFINED) {
        assert(resource->textureInfo.format == texInfo.format && "Texture format mismatch");
        assert(resource->textureInfo.width == texInfo.width && "Texture width mismatch");
        assert(resource->textureInfo.height == texInfo.height && "Texture height mismatch");
        assert(resource->textureInfo.mipLevels == texInfo.mipLevels && "Texture mip level mismatch");
    }

    assert(texInfo.format != VK_FORMAT_UNDEFINED && "Texture info uses undefined format");
    resource->textureInfo = texInfo;
}

void RenderGraph::AliasTexture(const std::string& aliasName, const std::string& existingName)
{
    auto it = textureNameToIndex.find(existingName);
    assert(it != textureNameToIndex.end() && "Aliasing texture failed because existing texture doesn't exist");
    textureNameToIndex[aliasName] = it->second;
}

void RenderGraph::CreateBuffer(const std::string& name, VkDeviceSize size)
{
    BufferResource* buf = GetOrCreateBuffer(name);

    if (buf->bufferInfo.size != 0) {
        assert(buf->bufferInfo.size == size && "Buffer size mismatch");
    }

    buf->bufferInfo.size = size;
}

void RenderGraph::ImportTexture(const std::string& name,
                                VkImage image,
                                VkImageView view,
                                const TextureInfo& info,
                                VkImageUsageFlags usage,
                                VkImageLayout initialLayout,
                                VkPipelineStageFlags2 initialStage,
                                VkImageLayout finalLayout)
{
    TextureResource* tex = GetOrCreateTexture(name);
    tex->textureInfo = info;
    tex->accumulatedUsage = usage;

    if (!tex->HasPhysical()) {
        uint32_t foundIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.size(); i++) {
            auto& phys = physicalResources[i];
            if (phys.bIsImported && phys.image == image) {
                foundIndex = i;
                assert(phys.dimensions.format == info.format && "Reimported image format mismatch");
                assert(phys.dimensions.width == info.width && "Reimported image width mismatch");
                assert(phys.dimensions.height == info.height && "Reimported image height mismatch");
                assert(phys.dimensions.levels == info.mipLevels && "Reimported image mip level mismatch");
                break;
            }
        }

        if (foundIndex != UINT32_MAX) {
            tex->physicalIndex = foundIndex;
        }
        else {
            tex->physicalIndex = physicalResources.size();
            physicalResources.emplace_back();
            auto& phys = physicalResources[tex->physicalIndex];
            phys.image = image;
            phys.imageView = view;
            phys.mipViews[0] = view;
            phys.bIsImported = true;

            phys.dimensions.type = ResourceDimensions::Type::Image;
            phys.dimensions.format = info.format;
            phys.dimensions.width = info.width;
            phys.dimensions.height = info.height;
            phys.dimensions.depth = 1;
            phys.dimensions.levels = info.mipLevels;
            phys.dimensions.layers = 1;
            phys.dimensions.samples = 1;
        }
    }

    auto& phys = physicalResources[tex->physicalIndex];
    tex->layout = initialLayout;
    phys.event.stages = initialStage;
    phys.event.access = VK_ACCESS_2_NONE;

    phys.aspect = VkHelpers::GetImageAspect(info.format);
    phys.dimensions.name = name;
    phys.usageChain.clear();
    tex->finalLayout = finalLayout;
}

void RenderGraph::ImportBufferNoBarrier(const std::string& name, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info)
{
    BufferResource* buf = GetOrCreateBuffer(name);
    buf->bufferInfo = info;
    buf->accumulatedUsage = info.usage;
    if (!buf->HasPhysical()) {
        uint32_t foundIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.size(); i++) {
            auto& phys = physicalResources[i];
            if (phys.bIsImported && phys.buffer == buffer) {
                foundIndex = i;
                assert(phys.dimensions.bufferSize == info.size && "Reimported buffer size mismatch");
                assert(phys.dimensions.bufferUsage == info.usage && "Reimported buffer usage mismatch");
                assert(phys.bufferAddress == address && "Reimported buffer address mismatch");
                assert(phys.addressRetrieved && "Reimported buffer not marked as address retrieved");
                break;
            }
        }

        if (foundIndex != UINT32_MAX) {
            buf->physicalIndex = foundIndex;
        }
        else {
            buf->physicalIndex = physicalResources.size();
            physicalResources.emplace_back();
            auto& phys = physicalResources[buf->physicalIndex];
            phys.buffer = buffer;
            phys.bufferAddress = address;
            phys.bIsImported = true;

            phys.dimensions.type = ResourceDimensions::Type::Buffer;
            phys.dimensions.bufferSize = info.size;
            phys.dimensions.bufferUsage = info.usage;
        }
    }

    auto& phys = physicalResources[buf->physicalIndex];
    phys.dimensions.name = name;
    phys.usageChain.clear();
    phys.bDisableBarriers = true;
}

void RenderGraph::ImportBuffer(const std::string& name, VkBuffer buffer, VkDeviceAddress address, const BufferInfo& info, PipelineEvent initialState)
{
    BufferResource* buf = GetOrCreateBuffer(name);
    buf->bufferInfo = info;
    buf->accumulatedUsage = info.usage;
    if (!buf->HasPhysical()) {
        uint32_t foundIndex = UINT32_MAX;
        for (uint32_t i = 0; i < physicalResources.size(); i++) {
            auto& phys = physicalResources[i];
            if (phys.bIsImported && phys.buffer == buffer) {
                foundIndex = i;
                assert(phys.dimensions.bufferSize == info.size && "Reimported buffer size mismatch");
                assert(phys.dimensions.bufferUsage == info.usage && "Reimported buffer usage mismatch");
                assert(phys.bufferAddress == address && "Reimported buffer address mismatch");
                assert(phys.addressRetrieved && "Reimported buffer not marked as address retrieved");
                break;
            }
        }

        if (foundIndex != UINT32_MAX) {
            buf->physicalIndex = foundIndex;
        }
        else {
            buf->physicalIndex = physicalResources.size();
            physicalResources.emplace_back();
            auto& phys = physicalResources[buf->physicalIndex];
            phys.buffer = buffer;
            phys.bufferAddress = address;
            phys.bIsImported = true;

            phys.dimensions.type = ResourceDimensions::Type::Buffer;
            phys.dimensions.bufferSize = info.size;
            phys.dimensions.bufferUsage = info.usage;
        }
    }

    auto& phys = physicalResources[buf->physicalIndex];
    phys.event.stages = initialState.stages;
    phys.event.access = initialState.access;
    phys.dimensions.name = name;
    phys.usageChain.clear();
    phys.bDisableBarriers = false;
}

bool RenderGraph::HasTexture(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    return it != textureNameToIndex.end();
}

bool RenderGraph::HasBuffer(const std::string& name)
{
    auto it = bufferNameToIndex.find(name);
    return it != bufferNameToIndex.end();
}

VkImage RenderGraph::GetImageHandle(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].image;
}

VkImageView RenderGraph::GetImageViewHandle(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].imageView;
}

VkImageView RenderGraph::GetImageViewMipHandle(const std::string& name, uint32_t mipLevel)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");
    assert(mipLevel < RDG_MAX_MIP_LEVELS);

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].mipViews[mipLevel];
}

VkImageView RenderGraph::GetDepthOnlyImageViewHandle(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
        return phys.imageView;
    }

    assert(phys.depthOnlyView != VK_NULL_HANDLE && "Texture has no depth only view");
    return phys.depthOnlyView;
}

VkImageView RenderGraph::GetStencilOnlyImageViewHandle(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
        return phys.imageView;
    }

    assert(phys.stencilOnlyView != VK_NULL_HANDLE && "Texture has no stencil only view");
    return phys.stencilOnlyView;
}

const ResourceDimensions& RenderGraph::GetImageDimensions(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].dimensions;
}

const VkImageAspectFlags RenderGraph::GetImageAspect(const std::string& name)
{

    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].aspect;
}

uint32_t RenderGraph::GetSampledImageViewDescriptorIndex(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].sampledDescriptorHandle.index;
}

uint32_t RenderGraph::GetStorageImageViewDescriptorIndex(const std::string& name, uint32_t mipLevel)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].storageMipDescriptorHandles[mipLevel].index;
}

uint32_t RenderGraph::GetDepthOnlySampledImageViewDescriptorIndex(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");
    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
        return phys.sampledDescriptorHandle.index;
    }

    assert(phys.depthOnlyDescriptorHandle.IsValid() && "Texture has no depth only descriptor");
    return phys.depthOnlyDescriptorHandle.index;
}

uint32_t RenderGraph::GetStencilOnlyStorageImageViewDescriptorIndex(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");
    auto& phys = physicalResources[tex.physicalIndex];

    if (phys.aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
        return phys.sampledDescriptorHandle.index;
    }

    assert(phys.stencilOnlyDescriptorHandle.IsValid() && "Texture has no stencil only descriptor");
    return phys.stencilOnlyDescriptorHandle.index;
}

VkBuffer RenderGraph::GetBufferHandle(const std::string& name)
{
    auto it = bufferNameToIndex.find(name);
    assert(it != bufferNameToIndex.end() && "Buffer not found");

    auto& buf = buffers[it->second];
    assert(buf.HasPhysical() && "Buffer has no physical resource");

    return physicalResources[buf.physicalIndex].buffer;
}

VkDeviceAddress RenderGraph::GetBufferAddress(const std::string& name)
{
    auto it = bufferNameToIndex.find(name);
    assert(it != bufferNameToIndex.end() && "Buffer not found");

    auto& buf = buffers[it->second];
    assert(buf.HasPhysical() && "Buffer has no physical resource");

    auto& phys = physicalResources[buf.physicalIndex];

    if (!phys.addressRetrieved) {
        VkBufferDeviceAddressInfo info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        info.buffer = phys.buffer;
        phys.bufferAddress = vkGetBufferDeviceAddress(context->device, &info);
        phys.addressRetrieved = true;
    }

    return phys.bufferAddress;
}

PipelineEvent RenderGraph::GetBufferState(const std::string& name)
{
    auto it = bufferNameToIndex.find(name);
    assert(it != bufferNameToIndex.end() && "Buffer not found");

    auto& buf = buffers[it->second];
    assert(buf.HasPhysical() && "Buffer has no physical resource");

    return physicalResources[buf.physicalIndex].event;
}

void RenderGraph::CarryTextureToNextFrame(const std::string& name, const std::string& newName, VkImageUsageFlags additionalUsage)
{
    TextureResource* tex = GetOrCreateTexture(name);
    tex->bCanUseAliasedTexture = false;
    tex->accumulatedUsage |= additionalUsage;

    if (tex->physicalIndex != UINT32_MAX) {
        auto& phys = physicalResources[tex->physicalIndex];
        if (phys.IsAllocated()) {
            assert((phys.dimensions.bufferUsage & additionalUsage) == additionalUsage && "Existing physical texture usage is not a superset of required usage");
        }
    }

    for (const auto& c : textureCarryovers) {
        assert(c.srcName != name && "Source texture already designated for carryover");
        assert(c.dstName != newName && "Destination texture name already used in another carryover");
        if (const TextureResource* otherTex = GetTexture(c.srcName)) {
            assert(otherTex->index != tex->index && "Cannot carry over texture already marked to be carried over");
        }
    }

    textureCarryovers.emplace_back(name, newName);
}

void RenderGraph::CarryBufferToNextFrame(const std::string& name, const std::string& newName, VkBufferUsageFlags additionalUsage)
{
    BufferResource* buf = GetOrCreateBuffer(name);
    buf->bCanUseAliasedBuffer = false;
    buf->accumulatedUsage |= additionalUsage;

    if (buf->physicalIndex != UINT32_MAX) {
        auto& phys = physicalResources[buf->physicalIndex];
        if (phys.IsAllocated()) {
            assert((phys.dimensions.bufferUsage & additionalUsage) == additionalUsage && "Existing physical buffer usage is not a superset of required usage");
        }
    }

    for (const auto& c : bufferCarryovers) {
        assert(c.srcName != name && "Source buffer already designated for carryover");
        assert(c.dstName != newName && "Destination buffer name already used in another carryover");
    }

    bufferCarryovers.emplace_back(name, newName);
}

UploadAllocation RenderGraph::AllocateTransient(size_t size)
{
    TransientUploadArena& arena = uploadArenas[currentFrameIndex];
    size_t offset = arena.allocator.Allocate(size);

    if (offset == SIZE_MAX) {
        size_t required = arena.allocator.GetUsed() + size;
        size_t newSize = std::max(arena.size * 2, required);
        RecreateTransientArena(currentFrameIndex, newSize);
        offset = arena.allocator.Allocate(size);
        assert(offset != SIZE_MAX && "Still OOM after resize");
    }

    return {
        .ptr = static_cast<char*>(arena.buffer.allocationInfo.pMappedData) + offset,
        .address = arena.buffer.address + offset,
        .offset = offset
    };
}

void RenderGraph::RecreateTransientArena(uint32_t frameIndex, size_t newSize)
{
    TransientUploadArena& arena = uploadArenas[frameIndex];
    Core::LinearAllocator newAllocator = Core::LinearAllocator::CreateExpanded(arena.allocator, newSize);

    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    bufferInfo.size = newSize;
    AllocatedBuffer newBuffer = AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    newBuffer.SetDebugName(("frameBufferUploader_" + std::to_string(frameIndex)).c_str());

    memcpy(newBuffer.allocationInfo.pMappedData, arena.buffer.allocationInfo.pMappedData, arena.allocator.GetUsed());

    arena.buffer = std::move(newBuffer);
    arena.allocator = newAllocator;
    arena.size = newSize;
}

void RenderGraph::LogImageBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex) const
{
    if (!bDebugLogging) return;

    auto LayoutToString = [](VkImageLayout layout) -> const char* {
        switch (layout) {
            case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
            case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC";
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST";
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY";
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT";
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT";
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL: return "DEPTH_ATTACHMENT";
            case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL: return "STENCIL_ATTACHMENT";
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return "DEPTH_STENCIL_READ_ONLY";
            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL: return "DEPTH_READ_ONLY";
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC";
            default: return "UNKNOWN";
        }
    };

    SPDLOG_INFO("  [BARRIER] {} ({}): {} -> {}", resourceName, physicalIndex, LayoutToString(barrier.oldLayout), LayoutToString(barrier.newLayout));
}

void RenderGraph::LogBufferBarrier(const std::string& resourceName, VkAccessFlags2 access) const
{
    if (!bDebugLogging) return;

    const char* accessType = "read";
    if (access & (VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT)) {
        accessType = "write";
    }

    SPDLOG_INFO("  [BUFFER BARRIER] {} ({})", resourceName, accessType);
}


TextureResource* RenderGraph::GetTexture(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    if (it != textureNameToIndex.end()) {
        return &textures[it->second];
    }

    return nullptr;
}

TextureResource* RenderGraph::GetOrCreateTexture(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    if (it != textureNameToIndex.end()) {
        return &textures[it->second];
    }

    uint32_t index = textures.size();
    textures.push_back(TextureResource{
        .name = name,
        .index = index,
    });
    textureNameToIndex[name] = index;

    return &textures[index];
}

BufferResource* RenderGraph::GetBuffer(const std::string& name)
{
    auto it = bufferNameToIndex.find(name);
    if (it != bufferNameToIndex.end()) {
        return &buffers[it->second];
    }

    return nullptr;
}

BufferResource* RenderGraph::GetOrCreateBuffer(const std::string& name)
{
    auto it = bufferNameToIndex.find(name);
    if (it != bufferNameToIndex.end()) {
        return &buffers[it->second];
    }

    uint32_t index = buffers.size();
    buffers.push_back(BufferResource{
        .name = name,
        .index = index,
    });
    bufferNameToIndex[name] = index;

    return &buffers[index];
}

void RenderGraph::DestroyPhysicalResource(PhysicalResource& resource)
{
    if (resource.bIsImported) {
        return;
    }

    if (resource.dimensions.IsImage()) {
        for (uint32_t mip = 0; mip < resource.dimensions.levels; ++mip) {
            if (resource.mipViews[mip] != VK_NULL_HANDLE) {
                vkDestroyImageView(context->device, resource.mipViews[mip], nullptr);
                resource.mipViews[mip] = VK_NULL_HANDLE;
            }
            if (resource.storageMipDescriptorHandles[mip].IsValid()) {
                StorageImageType storageType = GetStorageImageType(resource.dimensions.format, resource.aspect);
                switch (storageType) {
                    case StorageImageType::Float4:
                        transientStorageFloat4HandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case StorageImageType::Float2:
                        transientStorageFloat2HandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case StorageImageType::Float:
                        transientStorageFloatHandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case StorageImageType::UInt4:
                        transientStorageUInt4HandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                    case StorageImageType::UInt:
                        transientStorageUIntHandleAllocator.Remove(resource.storageMipDescriptorHandles[mip]);
                        break;
                }
                resource.storageMipDescriptorHandles[mip] = {};
            }
        }

        if (resource.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, resource.imageView, nullptr);
            resource.imageView = VK_NULL_HANDLE;
        }
        if (resource.sampledDescriptorHandle.IsValid()) {
            transientSampledImageHandleAllocator.Remove(resource.sampledDescriptorHandle);
            resource.sampledDescriptorHandle = {};
        }

        if (resource.depthOnlyView != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, resource.depthOnlyView, nullptr);
            resource.depthOnlyView = VK_NULL_HANDLE;
        }
        if (resource.depthOnlyDescriptorHandle.IsValid()) {
            transientSampledImageHandleAllocator.Remove(resource.depthOnlyDescriptorHandle);
            resource.depthOnlyDescriptorHandle = {};
        }

        if (resource.stencilOnlyView != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, resource.stencilOnlyView, nullptr);
            resource.stencilOnlyView = VK_NULL_HANDLE;
        }
        if (resource.stencilOnlyDescriptorHandle.IsValid()) {
            transientStorageUIntHandleAllocator.Remove(resource.stencilOnlyDescriptorHandle);
            resource.stencilOnlyDescriptorHandle = {};
        }

        if (resource.image != VK_NULL_HANDLE) {
            vmaDestroyImage(context->allocator, resource.image, resource.imageAllocation);
            resource.image = VK_NULL_HANDLE;
            resource.imageAllocation = VK_NULL_HANDLE;
        }
    }
    else {
        if (resource.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context->allocator, resource.buffer, resource.bufferAllocation);
            resource.buffer = VK_NULL_HANDLE;
            resource.bufferAllocation = VK_NULL_HANDLE;
        }
    }

    resource.descriptorWritten = false;
    resource.addressRetrieved = false;
    resource.event = {};
}

void RenderGraph::CreatePhysicalImage(PhysicalResource& resource, const ResourceDimensions& dim)
{
    resource.debugName = fmt::format("PhysicalResource{}", debugNameCounter++);
    VkImageCreateInfo imageInfo = VkHelpers::ImageCreateInfo(
        dim.format,
        {dim.width, dim.height, dim.depth},
        dim.imageUsage
    );
    imageInfo.mipLevels = dim.levels;
    imageInfo.arrayLayers = dim.layers;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(dim.samples);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VK_CHECK(vmaCreateImage(context->allocator, &imageInfo, &allocInfo, &resource.image, &resource.imageAllocation, nullptr));
#ifdef _DEBUG
    VkDebugUtilsObjectNameInfoEXT nameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
    nameInfo.objectHandle = reinterpret_cast<uint64_t>(resource.image);
    nameInfo.pObjectName = resource.debugName.c_str();
    vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);
#endif

    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;

    if (dim.format == VK_FORMAT_D16_UNORM || dim.format == VK_FORMAT_D32_SFLOAT || dim.format == VK_FORMAT_X8_D24_UNORM_PACK32) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else if (dim.format == VK_FORMAT_D16_UNORM_S8_UINT || dim.format == VK_FORMAT_D24_UNORM_S8_UINT || dim.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else if (dim.format == VK_FORMAT_S8_UINT) {
        aspectFlags = VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    resource.aspect = aspectFlags;
    resource.dimensions = dim;
    resource.event = {};

    constexpr VkImageUsageFlags TRANSFER_ONLY = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((dim.imageUsage & ~TRANSFER_ONLY) == 0) {
        return;
    }


    VkImageViewCreateInfo viewInfo = VkHelpers::ImageViewCreateInfo(
        resource.image,
        dim.format,
        aspectFlags
    );

    VkImageViewCreateInfo sampledViewInfo = viewInfo;
    sampledViewInfo.subresourceRange.levelCount = dim.levels;
    VK_CHECK(vkCreateImageView(context->device, &sampledViewInfo, nullptr, &resource.imageView));

    if ((aspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) && (aspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT)) {
        // If Depth+Stencil, imageView is combined. Make 2 additional separate imageViews
        VkImageViewCreateInfo depthViewInfo = viewInfo;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewInfo.subresourceRange.levelCount = dim.levels;
        VK_CHECK(vkCreateImageView(context->device, &depthViewInfo, nullptr, &resource.depthOnlyView));

        VkImageViewCreateInfo stencilViewInfo = viewInfo;
        stencilViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        stencilViewInfo.subresourceRange.levelCount = dim.levels;
        VK_CHECK(vkCreateImageView(context->device, &stencilViewInfo, nullptr, &resource.stencilOnlyView));
    }

    for (uint32_t mip = 0; mip < dim.levels; ++mip) {
        VkImageViewCreateInfo mipViewInfo = viewInfo;
        mipViewInfo.subresourceRange.baseMipLevel = mip;
        mipViewInfo.subresourceRange.levelCount = 1;

        // For depth+stencil, mipViews are depth-only (stencil mips not supported)
        if ((aspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) && (aspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT)) {
            mipViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        VK_CHECK(vkCreateImageView(context->device, &mipViewInfo, nullptr, &resource.mipViews[mip]));
    }
}

void RenderGraph::CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim)
{
    resource.debugName = fmt::format("PhysicalResource{}", debugNameCounter++);
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dim.bufferSize;
    bufferInfo.usage = dim.bufferUsage;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VK_CHECK(vmaCreateBuffer(context->allocator, &bufferInfo, &allocInfo,
        &resource.buffer, &resource.bufferAllocation, nullptr));

    resource.dimensions = dim;
    resource.event = {};

#ifdef _DEBUG
    VkDebugUtilsObjectNameInfoEXT nameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
    nameInfo.objectHandle = reinterpret_cast<uint64_t>(resource.buffer);
    nameInfo.pObjectName = resource.debugName.c_str();
    vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);
#endif
}
} // Render
