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
    textures.reserve(MAX_TEXTURES);
    physicalResources.reserve(MAX_TEXTURES);
}

RenderGraph::~RenderGraph()
{
    for (auto& phys : physicalResources) {
        DestroyPhysicalResource(phys);
    }
}

RenderPass& RenderGraph::AddPass(const std::string& name)
{
    passes.push_back(std::make_unique<RenderPass>(*this, name));
    return *passes.back();
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

        for (auto* tex : pass->blitImageReads) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (auto* tex : pass->blitImageWrites) {
            tex->accumulatedUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for (auto& attachment : pass->colorAttachments) {
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
        for (auto* tex : pass->blitImageWrites) { UpdateTextureLifetime(tex); }
        for (auto* tex : pass->blitImageReads) { UpdateTextureLifetime(tex); }
        for (auto& attachment : pass->colorAttachments) { UpdateTextureLifetime(attachment); }
        if (pass->depthAttachment) { UpdateTextureLifetime(pass->depthAttachment); }

        for (auto& bufRead : pass->bufferReads) UpdateBufferLifetime(bufRead.resource);
    }
}

void RenderGraph::Compile()
{
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
            desiredDim.levels = 1;
            desiredDim.layers = 1;
            desiredDim.samples = 1;
            desiredDim.imageUsage = tex.accumulatedUsage;
            desiredDim.name = tex.name;

            // Try to find existing physical resource with matching dimensions
            bool foundAlias = false;
            for (uint32_t i = 0; i < physicalResources.size(); i++) {
                auto& phys = physicalResources[i];
                if (phys.bIsImported) { continue; }
                if (phys.dimensions != desiredDim) { continue; }

                // Physical usage must be a superset: (phys & required) == required
                if ((phys.dimensions.imageUsage & tex.accumulatedUsage) != tex.accumulatedUsage) { continue; }

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
            }
        }

        auto& phys = physicalResources[tex.physicalIndex];

        if (!phys.IsAllocated() && tex.textureInfo.format != VK_FORMAT_UNDEFINED) {
            CreatePhysicalImage(phys, phys.dimensions);
        }
    }

    // 2. Write descriptors only if not already written
    for (auto& phys : physicalResources) {
        if (phys.NeedsDescriptorWrite()) {
            phys.descriptorHandle = transientImageHandleAllocator.Add();
            assert(phys.descriptorHandle.IsValid() && "Invalid descriptor handle assigned to physical resource");

            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageImageDescriptor(
                phys.descriptorHandle.index, {nullptr, phys.view, VK_IMAGE_LAYOUT_GENERAL}
            );
            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledImageDescriptor(
                phys.descriptorHandle.index, {nullptr, phys.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
            );

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
            if (phys.event.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                auto barrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                );
                LogBarrier(barrier, attachment->name, attachment->physicalIndex);
                barriers.push_back(barrier);
            }
        }

        if (pass->depthAttachment) {
            auto& phys = GetPhysical(pass->depthAttachment);
            if (phys.event.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                auto barrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT), // May need stencil too
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                );
                LogBarrier(barrier, pass->depthAttachment->name, pass->depthAttachment->physicalIndex);
                barriers.push_back(barrier);
            }
        }

        for (auto* tex : pass->storageImageWrites) {
            auto& phys = GetPhysical(tex);
            if (phys.event.layout != VK_IMAGE_LAYOUT_GENERAL) {
                auto barrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
                );
                LogBarrier(barrier, tex->name, tex->physicalIndex);
                barriers.push_back(barrier);
            }
        }

        for (auto* tex : pass->storageImageReads) {
            auto& phys = GetPhysical(tex);
            if (phys.event.layout != VK_IMAGE_LAYOUT_GENERAL) {
                auto barrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
                );
                LogBarrier(barrier, tex->name, tex->physicalIndex);
                barriers.push_back(barrier);
            }
        }

        for (auto* tex : pass->sampledImageReads) {
            auto& phys = GetPhysical(tex);
            if (phys.event.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                auto barrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                );
                LogBarrier(barrier, tex->name, tex->physicalIndex);
                barriers.push_back(barrier);
            }
        }

        for (auto* tex : pass->blitImageReads) {
            auto& phys = GetPhysical(tex);
            if (phys.event.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
                auto barrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                );
                LogBarrier(barrier, tex->name, tex->physicalIndex);
                barriers.push_back(barrier);
            }
        }

        for (auto* tex : pass->blitImageWrites) {
            auto& phys = GetPhysical(tex);
            if (phys.event.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                auto barrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                );
                LogBarrier(barrier, tex->name, tex->physicalIndex);
                barriers.push_back(barrier);
            }
        }

        std::vector<VkBufferMemoryBarrier2> bufferBarriers;

        for (auto& bufRead : pass->bufferReads) {
            auto& phys = physicalResources[bufRead.resource->physicalIndex];
            VkAccessFlags2 desiredAccess = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            if (phys.event.stages != bufRead.stages || phys.event.access != desiredAccess) {
                VkBufferMemoryBarrier2 barrier = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .srcStageMask = phys.event.stages,
                    .srcAccessMask = phys.event.access,
                    .dstStageMask = bufRead.stages,
                    .dstAccessMask = desiredAccess,
                    .buffer = phys.buffer,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE
                };
                bufferBarriers.push_back(barrier);
                if (debugLogging) {
                    SPDLOG_INFO("  [BUFFER BARRIER] {}: stage change", bufRead.resource->name);
                }
            }
        }

        if (!barriers.empty() || !bufferBarriers.empty()) {
            if (debugLogging) {
                SPDLOG_INFO("  Inserting {} barrier(s)", barriers.size());
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
            pass->executeFunc(cmd);
        }


        for (auto& attachment : pass->colorAttachments) {
            auto& phys = GetPhysical(attachment);
            phys.event.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            phys.event.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if (pass->depthAttachment) {
            auto& phys = GetPhysical(pass->depthAttachment);
            phys.event.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            phys.event.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        for (auto* tex : pass->storageImageWrites) {
            auto& phys = GetPhysical(tex);
            phys.event.layout = VK_IMAGE_LAYOUT_GENERAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            phys.event.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }

        for (auto* tex : pass->blitImageWrites) {
            auto& phys = GetPhysical(tex);
            phys.event.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_BLIT_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        for (auto* tex : pass->blitImageReads) {
            auto& phys = GetPhysical(tex);
            phys.event.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            phys.event.stages = VK_PIPELINE_STAGE_2_BLIT_BIT;
            phys.event.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        }
        for (auto& bufRead : pass->bufferReads) {
            auto& phys = physicalResources[bufRead.resource->physicalIndex];
            phys.event.stages = bufRead.stages;
            phys.event.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
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
            if (phys.event.layout != tex.finalLayout) {
                auto finalBarrier = VkHelpers::ImageMemoryBarrier(
                    phys.image,
                    VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                    phys.event.stages, phys.event.access, phys.event.layout,
                    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE, tex.finalLayout
                );
                LogBarrier(finalBarrier, tex.name, tex.physicalIndex);
                finalBarriers.push_back(finalBarrier);
                phys.event.layout = tex.finalLayout;
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

void RenderGraph::Reset()
{
    passes.clear();
    textures.clear();
    textureNameToIndex.clear();
    buffers.clear();
    bufferNameToIndex.clear();

    for (auto& phys : physicalResources) {
        phys.logicalResourceIndices.clear();
    }

    for (auto it = importedImages.begin(); it != importedImages.end();) {
        if (--it->second.lifetime == 0) {
            auto& phys = physicalResources[it->second.physicalIndex];
            phys.image = VK_NULL_HANDLE;
            phys.view = VK_NULL_HANDLE;
            phys.bIsImported = false;
            transientImageHandleAllocator.Remove(phys.descriptorHandle);
            it = importedImages.erase(it);
        }
        else {
            ++it;
        }
    }

    for (auto it = importedBuffers.begin(); it != importedBuffers.end();) {
        if (--it->second.lifetime == 0) {
            auto& phys = physicalResources[it->second.physicalIndex];
            phys.buffer = VK_NULL_HANDLE;
            phys.bIsImported = false;
            it = importedBuffers.erase(it);
        }
        else {
            ++it;
        }
    }
}

void RenderGraph::InvalidateAll()
{
    textures.clear();
    textureNameToIndex.clear();
    buffers.clear();
    bufferNameToIndex.clear();

    passes.clear();
    importedImages.clear();
    importedBuffers.clear();


    for (PhysicalResource& physicalResource : physicalResources) {
        DestroyPhysicalResource(physicalResource);
    }
    physicalResources.clear();
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
        auto it = importedImages.find(image);
        if (it != importedImages.end()) {
            tex->physicalIndex = it->second.physicalIndex;
            auto& phys = physicalResources[it->second.physicalIndex];
            assert(phys.dimensions.format == info.format && "Reimported image format mismatch");
            assert(phys.dimensions.width == info.width && "Reimported image width mismatch");
            assert(phys.dimensions.height == info.height && "Reimported image height mismatch");

            it->second.lifetime = IMPORTED_RESOURCES_PHYSICAL_LIFETIME;
        }
        else {
            tex->physicalIndex = physicalResources.size();
            physicalResources.emplace_back();
            importedImages[image] = {tex->physicalIndex, IMPORTED_RESOURCES_PHYSICAL_LIFETIME};
            auto& phys = physicalResources[tex->physicalIndex];
            phys.image = image;
            phys.view = view;
            phys.bIsImported = true;

            phys.dimensions.type = ResourceDimensions::Type::Image;
            phys.dimensions.format = info.format;
            phys.dimensions.width = info.width;
            phys.dimensions.height = info.height;
            phys.dimensions.depth = 1;
            phys.dimensions.levels = 1;
            phys.dimensions.layers = 1;
            phys.dimensions.samples = 1;
        }
    }

    auto& phys = physicalResources[tex->physicalIndex];
    phys.event.layout = initialLayout;
    phys.event.stages = initialStage;
    phys.event.access = VK_ACCESS_2_NONE;

    phys.dimensions.name = name;

    tex->finalLayout = finalLayout;
}

void RenderGraph::ImportBuffer(const std::string& name, VkBuffer buffer, const BufferInfo& info, VkPipelineStageFlags2 initialStage)
{
    BufferResource* buf = GetOrCreateBuffer(name);
    buf->bufferInfo = info;

    if (!buf->HasPhysical()) {
        auto it = importedBuffers.find(buffer);
        if (it != importedBuffers.end()) {
            buf->physicalIndex = it->second.physicalIndex;
            auto& phys = physicalResources[it->second.physicalIndex];
            assert(phys.dimensions.bufferSize == info.size && "Reimported buffer size mismatch");
            assert(phys.dimensions.bufferUsage == info.usage && "Reimported buffer usage mismatch");
            it->second.lifetime = IMPORTED_RESOURCES_PHYSICAL_LIFETIME;
        }
        else {
            buf->physicalIndex = physicalResources.size();
            physicalResources.emplace_back();
            importedBuffers[buffer] = {buf->physicalIndex, IMPORTED_RESOURCES_PHYSICAL_LIFETIME};

            auto& phys = physicalResources[buf->physicalIndex];
            phys.buffer = buffer;
            phys.bIsImported = true;

            phys.dimensions.type = ResourceDimensions::Type::Buffer;
            phys.dimensions.bufferSize = info.size;
            phys.dimensions.bufferUsage = info.usage;
        }
    }

    auto& phys = physicalResources[buf->physicalIndex];
    phys.event.stages = initialStage;
    phys.event.access = VK_ACCESS_2_NONE;
    phys.dimensions.name = name;
}

VkImage RenderGraph::GetImage(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].image;
}

uint32_t RenderGraph::GetDescriptorIndex(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].descriptorHandle.index;
}

VkImageView RenderGraph::GetImageView(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasPhysical() && "Texture has no physical resource");

    return physicalResources[tex.physicalIndex].view;
}

VkDeviceAddress RenderGraph::GetBufferAddress(const std::string& name)
{
    auto it = bufferNameToIndex.find(name);
    assert(it != bufferNameToIndex.end() && "Buffer not found");

    auto& buf = buffers[it->second];
    assert(buf.HasPhysical() && "Buffer has no physical resource");

    auto& phys = physicalResources[buf.physicalIndex];

    // Retrieve address if not done yet (similar to descriptor writes in Compile)
    if (!phys.addressRetrieved) {
        VkBufferDeviceAddressInfo info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        info.buffer = phys.buffer;
        phys.bufferAddress = vkGetBufferDeviceAddress(context->device, &info);
        phys.addressRetrieved = true;
    }

    return phys.bufferAddress;
}

PipelineEvent RenderGraph::GetResourceState(const std::string& name) const
{
    auto it = textureNameToIndex.find(name);
    if (it == textureNameToIndex.end()) {
        return {};
    }

    auto& tex = textures[it->second];
    if (!tex.HasPhysical()) {
        return {};
    }

    return physicalResources[tex.physicalIndex].event;
}

void RenderGraph::LogBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex) const
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
        if (resource.view != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, resource.view, nullptr);
            resource.view = VK_NULL_HANDLE;
        }
        if (resource.image != VK_NULL_HANDLE) {
            vmaDestroyImage(context->allocator, resource.image, resource.imageAllocation);
            resource.image = VK_NULL_HANDLE;
            resource.imageAllocation = VK_NULL_HANDLE;
        }
        transientImageHandleAllocator.Remove(resource.descriptorHandle);
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
    VK_CHECK(vkCreateImageView(context->device, &viewInfo, nullptr, &resource.view));

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
