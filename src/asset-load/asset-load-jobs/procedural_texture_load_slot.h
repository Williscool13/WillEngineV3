//
// Created by William on 2026-05-21.
//

#ifndef WILL_ENGINE_PROCEDURAL_TEXTURE_LOAD_SLOT_H
#define WILL_ENGINE_PROCEDURAL_TEXTURE_LOAD_SLOT_H

#include <semaphore>
#include <volk.h>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"
#include "render/descriptors/procedural_texture_generate_resources.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;
struct ResourceManager;
class PipelineManager;
}

namespace Engine
{
struct Texture;
}

namespace AssetLoad
{
class ProceduralTextureLoadSlot
{
public:
    ProceduralTextureLoadSlot() = default;
    ~ProceduralTextureLoadSlot();

    void Initialize(
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        Render::ResourceManager* _resourceManager,
        Render::PipelineManager* _pipelineManager,
        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
        Core::InlineFunction<void(bool success, ProceduralTextureSlotHandle slotHandle)> notifyCallback);

    void Launch(ProceduralTextureSlotHandle handle, Engine::Texture* texture, StringID pipelineId);

    void Clear();

    ProceduralTextureSlotHandle slotHandle{};
    Engine::Texture* outputTexture{nullptr};

private:
    void Generate(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);
    void PostGenerateSetup();
    /**
     * Storage scratch the generate/downsample passes write; the output texture is a SAMPLED-only copy so it keeps DCC.
     */
    void EnsureScratch(uint32_t width, uint32_t height, uint32_t mipCount, VkFormat format);

    struct GenerateTask : enki::ITaskSet
    {
        ProceduralTextureLoadSlot* loadSlot{nullptr};

        explicit GenerateTask() : ITaskSet(1) { m_Priority = enki::TASK_PRIORITY_LOW; }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    GenerateTask task{};
    StringID pipelineId{};

    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};
    Render::PipelineManager* pipelineManager{nullptr};
    SubmitContext computeSubmit{};
    Render::AllocatedImage scratchImage{};
    Render::ImageView scratchViews[Render::ProceduralTextureGenerateResources::MAX_MIPS_PER_SLOT]{};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _dispatchCallback;
    Core::InlineFunction<void(bool success, ProceduralTextureSlotHandle slotHandle)> _notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_PROCEDURAL_TEXTURE_LOAD_SLOT_H
