//
// Created by William on 2025-12-27.
//

#include "render_graph.h"

#include <cassert>
#include <utility>

namespace Render
{
RenderPass& RenderGraph::AddPass(const std::string& name)
{
    passes.push_back(std::make_unique<RenderPass>(*this, name));
    return *passes.back();
}

void RenderGraph::Compile() {}

void RenderGraph::Execute(VkCommandBuffer cmd)
{}

void RenderGraph::Reset()
{
    passes.clear();
}

uint32_t RenderGraph::GetDescriptor(const std::string& name)
{
    auto it = textureNameToIndex.find(name);
    assert(it != textureNameToIndex.end() && "Texture not found");

    auto& tex = textures[it->second];
    if (!tex.HasDescriptor()) {
        // todo: allocate from free list
        tex.descriptorIndex = 0;
    }

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

RenderPass::RenderPass(RenderGraph& renderGraph, std::string name)
    : graph(renderGraph), renderPassName(std::move(name))
{}

RenderPass& RenderPass::WriteStorageImage(const std::string& name, const TextureInfo info)
{
    TextureResource* resource = graph.GetOrCreateTexture(name);

    if (info.format != VK_FORMAT_UNDEFINED) {
        if (resource->textureInfo.format == VK_FORMAT_UNDEFINED) {
            resource->textureInfo = info;
        }
        else {
            assert(resource->textureInfo.format == info.format && "Format mismatch");
            assert(resource->textureInfo.width == info.width && "Width mismatch");
            assert(resource->textureInfo.height == info.height && "Height mismatch");
        }
    }
    else {
        assert(resource->textureInfo.format != VK_FORMAT_UNDEFINED && "Texture not defined - provide TextureInfo on first use");
    }

    writtenStorageImages.push_back(resource);
    return *this;
}

RenderPass& RenderPass::Execute(std::function<void(VkCommandBuffer)> func)
{}
} // Render
