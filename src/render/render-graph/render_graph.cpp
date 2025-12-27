//
// Created by William on 2025-12-27.
//

#include "render_graph.h"

#include <cassert>
#include <utility>

#include "render_pass.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
RenderGraph::RenderGraph(VulkanContext* context, ResourceManager* resourceManager)
    : context(context), resourceManager(resourceManager)
{
    textures.reserve(MAX_TEXTURES);
    // todo: physical resources limit
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

void RenderGraph::Compile()
{
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
            desiredDim.imageUsage = tex.textureInfo.usage;
            desiredDim.name = tex.name;

            // Try to find existing physical resource with matching dimensions
            bool foundAlias = false;
            for (uint32_t i = 0; i < physicalResources.size(); i++) {
                auto& phys = physicalResources[i];

                if (phys.dimensions == desiredDim) {
                    // TODO: Also check if lifetimes are disjoint
                    tex.physicalIndex = i;
                    foundAlias = true;
                    break;
                }
            }

            if (!foundAlias) {
                // No alias found, allocate new physical resource
                tex.physicalIndex = physicalResources.size();
                physicalResources.emplace_back();
                physicalResources.back().dimensions = desiredDim;
            }
        }

        auto& phys = physicalResources[tex.physicalIndex];

        // Create image if physical resource isn't allocated yet
        if (!phys.IsAllocated() && tex.textureInfo.format != VK_FORMAT_UNDEFINED) {
            CreatePhysicalImage(phys, phys.dimensions);
        }
    }

    // 2. Write descriptors only if not already written
    for (auto& phys : physicalResources) {
        if (phys.NeedsDescriptorWrite()) {
            phys.descriptorIndex = static_cast<uint32_t>(&phys - &physicalResources[0]);

            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageImageDescriptor(
                phys.descriptorIndex, {nullptr, phys.view, VK_IMAGE_LAYOUT_GENERAL}
            );
            resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledImageDescriptor(
                phys.descriptorIndex, {nullptr, phys.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
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

        if (!barriers.empty()) {
            if (debugLogging) {
                SPDLOG_INFO("  Inserting {} barrier(s)", barriers.size());
            }
            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            depInfo.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &depInfo);
        }

        // Execute pass
        if (pass->executeFunc) {
            pass->executeFunc(cmd);
        }

        // Update events after pass execution
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
}

VkImage RenderGraph::GetImage(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    if (it == textureNameToIndex.end()) {
        SPDLOG_ERROR("Texture '{}' not found", name);
        return VK_NULL_HANDLE;
    }

    auto& tex = textures[it->second];
    if (!tex.HasPhysical()) {
        SPDLOG_ERROR("Texture '{}' has no physical resource", name);
        return VK_NULL_HANDLE;
    }

    return physicalResources[tex.physicalIndex].image;
}

uint32_t RenderGraph::GetDescriptorIndex(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    if (it == textureNameToIndex.end()) {
        SPDLOG_ERROR("Texture '{}' not found", name);
        return UINT32_MAX;
    }

    auto& tex = textures[it->second];
    if (!tex.HasPhysical()) {
        SPDLOG_ERROR("Texture '{}' has no physical resource", name);
        return UINT32_MAX;
    }

    return physicalResources[tex.physicalIndex].descriptorIndex;
}

void RenderGraph::ImportTexture(const std::string& name, VkImage image, VkImageView view, VkImageLayout initialLayout, VkPipelineStageFlags2 initialStage, VkImageLayout finalLayout)
{
    TextureResource* tex = GetOrCreateTexture(name);

    if (!tex->HasPhysical()) {
        tex->physicalIndex = physicalResources.size();
        physicalResources.emplace_back();
    }

    auto& phys = physicalResources[tex->physicalIndex];
    phys.image = image;
    phys.view = view;
    phys.event.layout = initialLayout;
    phys.event.stages = initialStage;
    phys.event.access = VK_ACCESS_2_NONE;
    phys.bIsImported = true;

    tex->finalLayout = finalLayout;
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

void RenderGraph::LogBarrier(const VkImageMemoryBarrier2& barrier, const std::string& resourceName, uint32_t physicalIndex)
{
    if (!debugLogging) return;

    auto LayoutToString = [](VkImageLayout layout) -> const char* {
        switch (layout) {
            case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
            case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC";
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST";
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY";
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC";
            default: return "UNKNOWN";
        }
    };

    SPDLOG_INFO("  [BARRIER] {} ({}): {} -> {}",
                resourceName,
                physicalIndex,
                LayoutToString(barrier.oldLayout),
                LayoutToString(barrier.newLayout));
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

void RenderGraph::DestroyPhysicalResource(PhysicalResource& resource)
{
    if (resource.bIsImported) {
        return;
    }

    if (resource.dimensions.is_image()) {
        if (resource.view != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, resource.view, nullptr);
            resource.view = VK_NULL_HANDLE;
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

    VkImageViewCreateInfo viewInfo = VkHelpers::ImageViewCreateInfo(
        resource.image,
        dim.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    VK_CHECK(vkCreateImageView(context->device, &viewInfo, nullptr, &resource.view));

    resource.dimensions = dim;
    resource.event = {}; // Reset state
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
