//
// Created by William on 2026-01-19.
//

#ifndef WILL_ENGINE_PIPELINE_LOAD_SLOT_H
#define WILL_ENGINE_PIPELINE_LOAD_SLOT_H

#include <volk.h>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"
#include "core/memory/memory_manager.h"

namespace Render
{
struct PipelineData;
struct VulkanContext;
struct ResourceManager;
}

namespace AssetLoad
{
class PipelineLoadSlot
{
public:
    PipelineLoadSlot();

    ~PipelineLoadSlot();

    void Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, VkPipelineCache _pipelineCache, Core::MemoryManager* _memoryManager,
                    Core::InlineFunction<void(bool success, PipelineSlotHandle slotHandle)> _notifyCallback);

    void Launch(PipelineSlotHandle slotHandle, Render::PipelineData* pipelineData);

    void Clear();

    Render::PipelineData* pipelineData{nullptr};

private:
    struct LoadPipelineTask : enki::ITaskSet
    {
        PipelineLoadSlot* loadSlot{nullptr};

        explicit LoadPipelineTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    PipelineSlotHandle pipelineSlotHandle{};

    LoadPipelineTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    VkPipelineCache pipelineCache{VK_NULL_HANDLE};
    Core::MemoryManager* memoryManager{nullptr};
    Core::InlineFunction<void(bool success, PipelineSlotHandle slotHandle)> notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_PIPELINE_LOAD_SLOT_H
