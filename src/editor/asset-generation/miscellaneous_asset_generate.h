//
// Created by William on 2026-02-17.
//

#ifndef WILL_ENGINE_MISCELLANEOUS_ASSET_GENERATE_H
#define WILL_ENGINE_MISCELLANEOUS_ASSET_GENERATE_H

#include <semaphore>
#include <vulkan/vulkan_core.h>

#include "core/containers/inline_function.h"
#include "core/containers/inline_path.h"
#include "core/memory/memory_manager.h"
#include "engine/core/texture_id.h"

namespace Render
{
struct ResourceManager;
struct VulkanContext;
class PipelineManager;
}

namespace Editor
{
void CreateCriticalEngineResources(Core::MemoryManager* memoryManager);

void CreateBRDFLookupTable(Core::MemoryManager* memoryManager,
                           Core::Path outputPath,
                           Engine::TextureID textureId,
                           Render::VulkanContext* context,
                           Render::ResourceManager* resourceManager,
                           Render::PipelineManager* pipelineManager,
                           Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback);
} // Editor

#endif //WILL_ENGINE_MISCELLANEOUS_ASSET_GENERATE_H
