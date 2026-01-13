//
// Created by William on 2025-12-27.
//

#include "render_graph.h"

#include <cassert>
#include <utility>

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

void RenderGraph::AccumulateTextureUsage() const
{
    for (auto& pass : passes) {
        for (auto* tex : pass->storageImageWrites) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        for (auto* tex : pass->storageImageReads) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        for (auto* tex : pass->sampledImageReads) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        for (auto* tex : pass->imageReadWrite) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        for (auto* tex : pass->clearImageWrites) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (auto* tex : pass->blitImageReads) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (auto* tex : pass->blitImageWrites) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (auto* tex : pass->copyImageReads) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (auto* tex : pass->copyImageWrites) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (auto* attachment : pass->colorAttachments) {
            attachment->accumulatedUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }

        if (pass->depthAttachment) {
            pass->depthAttachment->accumulatedUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
    }
}

void RenderGraph::CalculateLifetimes()
{
    for (uint32_t passIdx = 0; passIdx < passes.size(); passIdx++) {
        auto& pass = passes[passIdx];

        auto UpdateTextureLifetime = [passIdx](TextureResource* tex) {
            tex->firstPass = std::min(tex->firstPass, passIdx);
            tex->lastPass = std::max(tex->lastPass, passIdx);
        };

        auto UpdateBufferLifetime = [passIdx](BufferResource* buf) {
            buf->firstPass = std::min(buf->firstPass, passIdx);
            buf->lastPass = std::max(buf->lastPass, passIdx);
        };

        for (auto* tex : pass->storageImageWrites) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->storageImageReads) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->sampledImageReads) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->imageReadWrite) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->clearImageWrites) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->blitImageWrites) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->blitImageReads) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->copyImageReads) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->copyImageWrites) { UpdateTextureLifetime(tex); }
        for (auto& attachment : pass->colorAttachments) { UpdateTextureLifetime(attachment); }
        if (pass->depthAttachment) { UpdateTextureLifetime(pass->depthAttachment); }

        for (auto& buf : pass->bufferReads) { UpdateBufferLifetime(buf); }
        for (auto& buf : pass->bufferWrites) { UpdateBufferLifetime(buf); }
        for (auto& buf : pass->bufferReadTransfer) { UpdateBufferLifetime(buf); }
        for (auto& buf : pass->bufferWriteTransfer) { UpdateBufferLifetime(buf); }
        for (auto& buf : pass->bufferIndirectReads) { UpdateBufferLifetime(buf); }
        for (auto& buf : pass->bufferIndirectCountReads) { UpdateBufferLifetime(buf); }
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
                    phys.logicalResourceIndices.push_back(tex.index);
                    phys.lastLogicalResourceUse = tex.name;
                    if (!phys.IsAllocated()) {
                        phys.dimensions.imageUsage |= tex.accumulatedUsage;
                    }
                    foundAlias = true;
                    break;
                }
            }

            if (!foundAlias) {
                // No alias found, allocate new physical resource
                tex.physicalIndex = physicalResources.size();
                physicalResources.emplace_back();
                physicalResources.back().dimensions = desiredDim;
                physicalResources.back().logicalResourceIndices.push_back(tex.index);
                physicalResources.back().bCanAlias = tex.bCanUseAliasedTexture;
                physicalResources.back().lastLogicalResourceUse = tex.name;
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
            if (debugLogging) {
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

                if (phys.bIsImported) continue;
                if (!phys.bCanAlias) { continue; }
                if (!phys.dimensions.IsBuffer()) continue;

                if (phys.dimensions.bufferSize != desiredDim.bufferSize) continue;

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
                    foundAlias = true;
                    break;
                }
            }

            if (!foundAlias) {
                buf.physicalIndex = physicalResources.size();
                physicalResources.emplace_back();
                physicalResources.back().dimensions = desiredDim;
                physicalResources.back().logicalResourceIndices.push_back(buf.index);
                physicalResources.back().bCanAlias = buf.bCanUseAliasedBuffer;
                physicalResources.back().lastLogicalResourceUse = buf.name;
            }
        }
    }

    for (auto& buf : buffers) {
        if (buf.accumulatedUsage == 0) continue;

        auto& phys = physicalResources[buf.physicalIndex];
        if (!phys.IsAllocated() && buf.bufferInfo.size > 0) {
            CreatePhysicalBuffer(phys, phys.dimensions);
        }
        phys.lastUsedFrame = currentFrame;
    }

    for (auto& phys : physicalResources) {
        if (phys.NeedsDescriptorWrite()) {
            phys.sampledDescriptorHandle = transientImageHandleAllocator.Add();
            assert(phys.sampledDescriptorHandle.IsValid() && "Invalid descriptor handle assigned to physical resource");

            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledImageDescriptor(
                phys.sampledDescriptorHandle.index, {nullptr, phys.imageView, VK_IMAGE_LAYOUT_GENERAL}
            );

            for (uint32_t mip = 0; mip < phys.dimensions.levels; ++mip) {
                phys.storageMipDescriptorHandles[mip] = transientStorageImageHandleAllocator.Add();
                assert(phys.storageMipDescriptorHandles[mip].IsValid() && "Invalid descriptor handle assigned to physical resource view");

                resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageImageDescriptor(
                    phys.storageMipDescriptorHandles[mip].index,
                    {nullptr, phys.mipViews[mip], VK_IMAGE_LAYOUT_GENERAL}
                );
            }

            phys.descriptorWritten = true;
        }

        if (phys.NeedsAddressRetrieval()) {
            VkBufferDeviceAddressInfo info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            info.buffer = phys.buffer;
            phys.bufferAddress = vkGetBufferDeviceAddress(context->device, &info);
            phys.addressRetrieved = true;
        }
    }
}

void RenderGraph::Execute(VkCommandBuffer cmd)
{
    if (debugLogging) {
        SPDLOG_INFO("=== RenderGraph Execution ===");
    }

    for (auto& pass : passes) {
        if (debugLogging) {
            SPDLOG_INFO("[PASS] {}", pass->renderPassName);
        }
        std::vector<VkImageMemoryBarrier2> barriers;

        // Helper lambda to get physical resource
        auto GetPhysical = [this](TextureResource* tex) -> PhysicalResource& {
            return physicalResources[tex->physicalIndex];
        };

        for (auto& attachment : pass->colorAttachments) {
            auto& phys = GetPhysical(attachment);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, attachment->layout,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            );
            LogImageBarrier(barrier, attachment->name, attachment->physicalIndex);
            barriers.push_back(barrier);
        }

        if (pass->depthAttachment) {
            auto& phys = GetPhysical(pass->depthAttachment);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, pass->depthAttachment->layout,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            );
            LogImageBarrier(barrier, pass->depthAttachment->name, pass->depthAttachment->physicalIndex);
            barriers.push_back(barrier);
        }

        for (auto* tex : pass->storageImageWrites) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                pass->stages, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }

        for (auto* tex : pass->storageImageReads) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                pass->stages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }

        for (auto* tex : pass->sampledImageReads) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                pass->stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }

        for (auto* tex : pass->imageReadWrite) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                pass->stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }

        for (auto* tex : pass->blitImageReads) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }
        for (auto* tex : pass->clearImageWrites) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }
        for (auto* tex : pass->blitImageWrites) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }
        for (auto* tex : pass->copyImageReads) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }

        for (auto* tex : pass->copyImageWrites) {
            auto& phys = GetPhysical(tex);
            auto barrier = VkHelpers::ImageMemoryBarrier(
                phys.image,
                VkHelpers::SubresourceRange(phys.aspect),
                phys.event.stages, phys.event.access, tex->layout,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            LogImageBarrier(barrier, tex->name, tex->physicalIndex);
            barriers.push_back(barrier);
        }

        std::vector<VkBufferMemoryBarrier2> bufferBarriers;

        for (auto& buf : pass->bufferWrites) {
            auto& phys = physicalResources[buf->physicalIndex];
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
            LogBufferBarrier(buf->name, desiredAccess);
        }
        for (auto& bufWrite : pass->bufferWriteTransfer) {
            auto& phys = physicalResources[bufWrite->physicalIndex];
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
            LogBufferBarrier(bufWrite->name, desiredAccess);
        }

        for (auto& bufRead : pass->bufferReadTransfer) {
            auto& phys = physicalResources[bufRead->physicalIndex];
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
            LogBufferBarrier(bufRead->name, desiredAccess);
        }

        for (auto& bufRead : pass->bufferReads) {
            auto& phys = physicalResources[bufRead->physicalIndex];
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
            LogBufferBarrier(bufRead->name, desiredAccess);
        }

        for (auto& bufRead : pass->bufferIndirectReads) {
            auto& phys = physicalResources[bufRead->physicalIndex];
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
            LogBufferBarrier(bufRead->name, desiredAccess);
        }

        for (auto& bufRead : pass->bufferIndirectCountReads) {
            auto& phys = physicalResources[bufRead->physicalIndex];
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
            LogBufferBarrier(bufRead->name, desiredAccess);
        }

        if (!barriers.empty() || !bufferBarriers.empty()) {
            if (debugLogging) {
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


        for (auto& attachment : pass->colorAttachments) {
            auto& phys = GetPhysical(attachment);
            attachment->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            phys.event.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if (pass->depthAttachment) {
            auto& phys = GetPhysical(pass->depthAttachment);
            pass->depthAttachment->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            phys.event.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        for (auto* tex : pass->storageImageWrites) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_GENERAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }
        for (auto* tex : pass->storageImageReads) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_GENERAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }
        for (auto* tex : pass->sampledImageReads) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }
        for (auto* tex : pass->imageReadWrite) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_GENERAL;
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }

        for (auto* tex : pass->clearImageWrites) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_CLEAR_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }
        for (auto* tex : pass->blitImageWrites) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_BLIT_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }
        for (auto* tex : pass->blitImageReads) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_BLIT_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        for (auto* tex : pass->copyImageWrites) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        for (auto* tex : pass->copyImageReads) {
            auto& phys = GetPhysical(tex);
            tex->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        for (auto& bufWrite : pass->bufferWrites) {
            auto& phys = physicalResources[bufWrite->physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_WRITE_BIT;
        }
        for (auto& bufWrite : pass->bufferWriteTransfer) {
            auto& phys = physicalResources[bufWrite->physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        for (auto& bufRead : pass->bufferReads) {
            auto& phys = physicalResources[bufRead->physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_SHADER_READ_BIT;
        }

        for (auto& bufRead : pass->bufferReadTransfer) {
            auto& phys = physicalResources[bufRead->physicalIndex];
            phys.event.stages = pass->stages;
            phys.event.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        for (auto& bufRead : pass->bufferIndirectReads) {
            auto& phys = physicalResources[bufRead->physicalIndex];
            phys.event.stages = pass->stages | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            phys.event.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        }
        for (auto& bufRead : pass->bufferIndirectCountReads) {
            auto& phys = physicalResources[bufRead->physicalIndex];
            phys.event.stages = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            phys.event.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }
    }

    // Final barriers for imported resources
    if (debugLogging) {
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

void RenderGraph::Reset(uint64_t currentFrame, uint64_t maxFramesUnused)
{
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
        phys.lastLogicalResourceUse = newTex->name;
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
        phys.lastLogicalResourceUse = newBuf->name;
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
    transientImageHandleAllocator.Clear();
    textureCarryovers.clear();
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
    phys.lastLogicalResourceUse = name;

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
    phys.lastLogicalResourceUse = name;
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
    phys.lastLogicalResourceUse = name;
    phys.bDisableBarriers = false;
}

bool RenderGraph::HasTexture(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    return it != textureNameToIndex.end();
}

VkImage RenderGraph::GetImage(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].image;
}

VkImageView RenderGraph::GetImageView(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].imageView;
}

VkImageView RenderGraph::GetImageViewMip(const std::string& name, uint32_t mipLevel) {
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");
    assert(mipLevel < RDG_MAX_MIP_LEVELS);

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].mipViews[mipLevel];
}

const ResourceDimensions& RenderGraph::GetImageDimensions(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].dimensions;
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

void RenderGraph::LogImageBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex) const
{
    if (!debugLogging) return;

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
    if (!debugLogging) return;

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
        for (VkImageView& view : resource.mipViews) {
            vkDestroyImageView(context->device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (resource.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, resource.imageView, nullptr);
            resource.imageView = VK_NULL_HANDLE;
        }
        if (resource.image != VK_NULL_HANDLE) {
            vmaDestroyImage(context->allocator, resource.image, resource.imageAllocation);
            resource.image = VK_NULL_HANDLE;
            resource.imageAllocation = VK_NULL_HANDLE;
        }
        transientImageHandleAllocator.Remove(resource.sampledDescriptorHandle);
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
    VkImageCreateInfo imageInfo = VkHelpers::ImageCreateInfo(
        dim.format,
        {dim.width, dim.height, dim.depth},
        dim.imageUsage
    );
    imageInfo.mipLevels = dim.levels;
    imageInfo.arrayLayers = dim.layers;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(dim.samples);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(context->allocator, &imageInfo, &allocInfo,
        &resource.image, &resource.imageAllocation, nullptr));

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

    VkImageViewCreateInfo viewInfo = VkHelpers::ImageViewCreateInfo(
        resource.image,
        dim.format,
        aspectFlags
    );

    VkImageViewCreateInfo sampledViewInfo = viewInfo;
    sampledViewInfo.subresourceRange.levelCount = dim.levels;
    VK_CHECK(vkCreateImageView(context->device, &sampledViewInfo, nullptr, &resource.imageView));

    for (uint32_t mip = 0; mip < dim.levels; ++mip) {
        VkImageViewCreateInfo mipViewInfo = viewInfo;
        mipViewInfo.subresourceRange.baseMipLevel = mip;
        mipViewInfo.subresourceRange.levelCount = 1;
        VK_CHECK(vkCreateImageView(context->device, &mipViewInfo, nullptr, &resource.mipViews[mip]));
    }

    resource.aspect = aspectFlags;
    resource.dimensions = dim;
    resource.event = {};
}

void RenderGraph::CreatePhysicalBuffer(PhysicalResource& resource, const ResourceDimensions& dim)
{
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
}
} // Render
