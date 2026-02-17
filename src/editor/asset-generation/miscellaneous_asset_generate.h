//
// Created by William on 2026-02-17.
//

#ifndef WILL_ENGINE_MISCELLANEOUS_ASSET_GENERATE_H
#define WILL_ENGINE_MISCELLANEOUS_ASSET_GENERATE_H

#include <filesystem>
#include <functional>
#include <semaphore>
#include <vulkan/vulkan_core.h>

namespace Render
{
struct ResourceManager;
struct VulkanContext;
class PipelineManager;
}

namespace Editor
{
void CreateBRDFLookupTable(std::filesystem::path outputPath,
                           Render::VulkanContext* context,
                           Render::ResourceManager* resourceManager,
                           Render::PipelineManager* pipelineManager,
                           std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback);
} // Editor

#endif //WILL_ENGINE_MISCELLANEOUS_ASSET_GENERATE_H
