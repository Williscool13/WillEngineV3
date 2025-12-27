//
// Created by William on 2025-12-27.
//

#include "render_graph.h"

#include <cassert>
#include <utility>

#include "render_pass.h"
#include "render/resource_manager.h"

namespace Render
{
RenderGraph::RenderGraph(VulkanContext* context, ResourceManager* resourceManager)
    : context(context), resourceManager(resourceManager)
{}

RenderPass& RenderGraph::AddPass(const std::string& name)
{
    passes.push_back(std::make_unique<RenderPass>(*this, name));
    return *passes.back();
}

void RenderGraph::Compile()
{
    /*for (auto& tex : textures) {
        tex.usageType = TextureUsageType::Unknown;
        tex.descriptorIndex = UINT32_MAX;
    }*/

    for (auto& tex : textures) {
        if (!tex.IsAllocated()) {
            VkImageCreateInfo drawImageCreateInfo = VkHelpers::ImageCreateInfo(tex.textureInfo.format, {tex.textureInfo.width, tex.textureInfo.height, 1}, tex.textureInfo.usage);
            tex.image = AllocatedImage::CreateAllocatedImage(context, drawImageCreateInfo);
            VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
                tex.image.handle,
                tex.textureInfo.format,
                VK_IMAGE_ASPECT_COLOR_BIT
            );

            tex.view = ImageView::CreateImageView(context, viewInfo);
        }
    }
    for (auto& tex : textures) {
        if (tex.HasDescriptor() && tex.IsAllocated()) {
            VkImageLayout descriptorLayout;

            if (tex.usageType == TextureUsageType::Storage) {
                descriptorLayout = VK_IMAGE_LAYOUT_GENERAL;
                resourceManager->bindlessRDGTransientDescriptorBuffer.WriteStorageImageDescriptor(tex.descriptorIndex, {nullptr, tex.view.handle, descriptorLayout});
            }
            else {
                // todo: will also need an accompanying sampler for this. Perhaps just make a few defaults (linear and nearest) and choose between them
                descriptorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                resourceManager->bindlessRDGTransientDescriptorBuffer.WriteSampledImageDescriptor(tex.descriptorIndex, {nullptr, tex.view.handle, descriptorLayout});
            }
        }
    }
}

void RenderGraph::Execute(VkCommandBuffer cmd)
{
    for (auto& pass : passes) {
        if (pass->executeFunc) {
            pass->executeFunc(cmd);
        }
    }
}

void RenderGraph::Reset()
{
    passes.clear();
    storageDescriptorAllocator = 0;
    sampledDescriptorAllocator = 0;
}

uint32_t RenderGraph::GetStorageDescriptor()
{
    return storageDescriptorAllocator++;
}

uint32_t RenderGraph::GetSampledDescriptor(const std::string& name)
{
    auto& tex = textures[textureNameToIndex[name]];

    if (!tex.HasDescriptor()) {
        tex.usageType = TextureUsageType::Sampled;
        tex.descriptorIndex = sampledDescriptorAllocator++;
    }

    assert(tex.usageType == TextureUsageType::Sampled);
    return tex.descriptorIndex;
}

uint32_t RenderGraph::GetStorageDescriptorIndex(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    assert(tex.HasDescriptor() && "Texture has no been assigned a descriptor index");

    return tex.descriptorIndex;
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
        .index = index
    });
    textureNameToIndex[name] = index;

    return &textures[index];
}
} // Render
