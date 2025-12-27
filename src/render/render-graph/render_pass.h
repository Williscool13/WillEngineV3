//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_PASS_H
#define WILL_ENGINE_RENDER_PASS_H
#include <string>
#include <vector>

#include "render_graph.h"

namespace Render
{
struct TextureResource;

class RenderPass
{
public:
    RenderPass(RenderGraph& renderGraph, std::string name);

    RenderPass& WriteStorageImage(const std::string& name, TextureInfo info = {}, bool bAsDescriptor = false);

    RenderPass& Execute(std::function<void(VkCommandBuffer)> func);

private:
    friend class RenderGraph;
    RenderGraph& graph;
    std::string renderPassName;

    std::vector<TextureResource*> writtenStorageImages;
    std::function<void(VkCommandBuffer_T*)> executeFunc;
};
} // Render

#endif //WILL_ENGINE_RENDER_PASS_H